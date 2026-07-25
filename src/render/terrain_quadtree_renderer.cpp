#include <monkey_dust/render/terrain_quadtree_renderer.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/render/gpu_device.h>
#include <cstring>
#include <cstdio>

// Vertex-stage constant buffer — MUST mirror QuadVert in
// shaders/slang/terrain_quadtree.slang field-for-field (same byte-offset
// discipline as TerrainVertUBO/TerrainFwdVert, see terrain_renderer.h).
struct QuadVertUBO {
    float vp[16];          // 64B, float4x4
    float origin_x;        // one float4 slot: origin_x,origin_z,size_m,height_min_m
    float origin_z;
    float size_m;
    float height_min_m;
    float height_max_m;    // next float4 slot: height_max_m + 3 pad floats
    float _pad0, _pad1, _pad2;
};
static_assert(sizeof(QuadVertUBO) == 96, "QuadVertUBO size mismatch");

bool TerrainQuadtreeRenderer::BuildUnitPatchMesh() {
    constexpr int N  = kPatchN;
    constexpr int VC = (N + 1) * (N + 1);
    constexpr int IC = N * N * 6;
    static float    verts[VC * 2];
    static uint32_t idx[IC];

    for (int row = 0; row <= N; ++row) {
        for (int col = 0; col <= N; ++col) {
            int vi = row * (N + 1) + col;
            verts[vi * 2 + 0] = (float)col / (float)N;
            verts[vi * 2 + 1] = (float)row / (float)N;
        }
    }
    int ii = 0;
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            uint32_t v00 = (uint32_t)(row * (N + 1) + col);
            uint32_t v10 = v00 + 1;
            uint32_t v01 = v00 + (uint32_t)(N + 1);
            uint32_t v11 = v01 + 1;
            idx[ii++] = v00; idx[ii++] = v01; idx[ii++] = v10;
            idx[ii++] = v10; idx[ii++] = v01; idx[ii++] = v11;
        }
    }
    patch_vbo_.Init(0x8892u /*GL_ARRAY_BUFFER*/, verts, sizeof(verts));
    patch_ibo_.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, idx, sizeof(idx));
    patch_idx_count_ = (uint32_t)IC;
    return true;
}

bool TerrainQuadtreeRenderer::UploadHeightmap(int zx, int zy, float& out_height_min, float& out_height_max) {
    constexpr int N = 65; // zone native resolution: col,row in 0..64
    static float   h_tmp[N * N];
    static uint8_t rgba[N * N * 4];

    float hmin = 1e9f, hmax = -1e9f;
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            float h = TerrainAtlas_GetHeight(zx, zy, col, row);
            h_tmp[row * N + col] = h;
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }
    float range = hmax - hmin;
    if (range < 1.f) range = 1.f; // avoid div-by-zero on a perfectly flat zone

    // Pack uint16 height into R(low byte)+G(high byte) of an RGBA8 texture
    // (see header doc comment — this HAL has no R16 GPU format yet).
    for (int i = 0; i < N * N; ++i) {
        float    norm = (h_tmp[i] - hmin) / range;
        uint32_t h16  = (uint32_t)(norm * 65535.f + 0.5f);
        if (h16 > 65535u) h16 = 65535u;
        rgba[i * 4 + 0] = (uint8_t)(h16 & 0xFFu);
        rgba[i * 4 + 1] = (uint8_t)((h16 >> 8) & 0xFFu);
        rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 255;
    }

    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = false;
    sd.flip_v     = false;

    GpuTexture tex;
    if (!tex.InitFromMemory(rgba, N, N, sd)) return false;
    height_tex_     = tex.TakeSDLTexture();
    height_sampler_ = tex.TakeSDLSampler();
    out_height_min  = hmin;
    out_height_max  = hmin + range;
    return true;
}

bool TerrainQuadtreeRenderer::Init(int zx, int zy, float& out_height_min, float& out_height_max) {
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_quadtree.vert";
    pd.frag_path = "shaders/terrain_quadtree.frag";

    pd.layout.count      = 1;
    pd.layout.stride     = 8; // float2 aUV
    pd.layout.attribs[0] = { 0, 0, GpuAttribFmt::F2 };

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1; // slot 0: QuadVertUBO
    pd.vert_samplers     = 1; // heightTex — VTF, confirmed safe 2026-07-25
    pd.frag_uniform_bufs = 0;
    pd.frag_samplers     = 0;

    if (!pipeline_.Create(pd)) {
        fprintf(stderr, "[TerrainQuadtreeRenderer] pipeline create failed\n");
        return false;
    }
    if (!BuildUnitPatchMesh()) return false;
    if (!UploadHeightmap(zx, zy, out_height_min, out_height_max)) return false;
    ready_ = true;
    return true;
}

void TerrainQuadtreeRenderer::Shutdown() {
    if (height_tex_)     { SDL_ReleaseGPUTexture(md::GpuDevice::Get().SDLDevice(), height_tex_); height_tex_ = nullptr; }
    if (height_sampler_) { SDL_ReleaseGPUSampler(md::GpuDevice::Get().SDLDevice(), height_sampler_); height_sampler_ = nullptr; }
    patch_vbo_.Shutdown();
    patch_ibo_.Shutdown();
    pipeline_.Destroy();
    ready_ = false;
}

void TerrainQuadtreeRenderer::Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                    const float* vp16,
                                    float origin_x, float origin_z, float size_m,
                                    float height_min_m, float height_max_m) {
    if (!ready_ || !height_tex_ || !height_sampler_) return;

    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb{ patch_vbo_.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib{ patch_ibo_.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    QuadVertUBO vubo{};
    memcpy(vubo.vp, vp16, 64);
    vubo.origin_x     = origin_x;
    vubo.origin_z     = origin_z;
    vubo.size_m       = size_m;
    vubo.height_min_m = height_min_m;
    vubo.height_max_m = height_max_m;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    SDL_GPUTextureSamplerBinding vtsb{ height_tex_, height_sampler_ };
    SDL_BindGPUVertexSamplers(rp, 0, &vtsb, 1);

    SDL_DrawGPUIndexedPrimitives(rp, patch_idx_count_, 1, 0, 0, 0);
}
#endif // MD_SDL_GPU
