#include <monkey_dust/render/terrain_quadtree_renderer.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/render/gpu_device.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

// Vertex-stage constant buffer — MUST mirror QuadVert in
// shaders/slang/terrain_quadtree.slang field-for-field (same byte-offset
// discipline as TerrainVertUBO/TerrainFwdVert, see terrain_renderer.h).
struct QuadVertUBO {
    float vp[16];              // 64B, float4x4
    float origin_x;            // float4 slot: origin_x,origin_z,size_m,height_min_m
    float origin_z;
    float size_m;
    float height_min_m;
    float height_max_m;        // float4 slot: height_max_m,region_origin_x,region_origin_z,region_size
    float region_origin_x;
    float region_origin_z;
    float region_size;
    float morph_t;             // float4 slot: morph_t,grid_n,pad,pad
    float grid_n;
    float _pad0, _pad1;
};
static_assert(sizeof(QuadVertUBO) == 112, "QuadVertUBO size mismatch");

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

// TerrainAtlas zones are 65x65 samples (col,row in 0..64) — 64 steps/zone,
// matching TERRAIN_GRID_LEVELS-independent atlas resolution, not the
// per-chunk render mesh's own TERRAIN_GRID (128). See terrain_gen.h's
// "col,row in 0..64" doc comment.
static constexpr int kZoneGridStep64 = 64;

// Maps a global region-grid sample index (0..zone_span*64) to (zone index,
// local col/row 0..64) — zones share their boundary vertex (zone Z's
// col=64 == zone Z+1's col=0), so only the very last global sample needs
// the "clamp into the last zone's col=64" special case.
static void s_region_sample_to_zone(int gi, int zone_span, int& zi, int& local) {
    zi    = gi / kZoneGridStep64;
    local = gi % kZoneGridStep64;
    if (zi >= zone_span) { zi = zone_span - 1; local = kZoneGridStep64; }
}

bool TerrainQuadtreeRenderer::UploadHeightmapRegion(int zx0, int zy0, int zone_span,
                                                     float& out_height_min, float& out_height_max) {
    constexpr int kMaxRes = 1025; // hard cap: zone_span<=16 -> 1025 samples/axis
    const int N = zone_span * kZoneGridStep64 + 1;
    if (N > kMaxRes) {
        fprintf(stderr, "[TerrainQuadtreeRenderer] region too large: zone_span=%d -> %dx%d samples (cap %d)\n",
                zone_span, N, N, kMaxRes);
        return false;
    }
    static float   h_tmp[kMaxRes * kMaxRes];
    static uint8_t rgba[kMaxRes * kMaxRes * 4];

    float hmin = 1e9f, hmax = -1e9f;
    for (int row = 0; row < N; ++row) {
        int zzi, zlocal_row;
        s_region_sample_to_zone(row, zone_span, zzi, zlocal_row);
        for (int col = 0; col < N; ++col) {
            int zxi, zlocal_col;
            s_region_sample_to_zone(col, zone_span, zxi, zlocal_col);
            float h = TerrainAtlas_GetHeight(zx0 + zxi, zy0 + zzi, zlocal_col, zlocal_row);
            h_tmp[row * N + col] = h;
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }
    float range = hmax - hmin;
    if (range < 1.f) range = 1.f; // avoid div-by-zero on a perfectly flat region

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

bool TerrainQuadtreeRenderer::Init(int zx0, int zy0, int zone_span,
                                    float& out_height_min, float& out_height_max) {
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
    if (!UploadHeightmapRegion(zx0, zy0, zone_span, out_height_min, out_height_max)) return false;

    region_size_     = (float)zone_span * CHUNK_SIZE;
    region_origin_x_ = (float)zx0 * CHUNK_SIZE;
    region_origin_z_ = (float)zy0 * CHUNK_SIZE;

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

float TerrainQuadtreeRenderer::ComputeMorphT(float dist, int depth, const float* lod_distances) {
    float near_d = lod_distances[depth];
    float far_d  = (depth == 0) ? (lod_distances[0] * 2.f) : lod_distances[depth - 1];
    if (far_d <= near_d) return 0.f; // degenerate config — never morph rather than divide by ~0
    float t = (dist - near_d) / (far_d - near_d);
    return std::max(0.f, std::min(1.f, t));
}

void TerrainQuadtreeRenderer::Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                    const float* vp16,
                                    float origin_x, float origin_z, float size_m,
                                    float morph_t,
                                    float height_min_m, float height_max_m) {
    if (!ready_ || !height_tex_ || !height_sampler_) return;

    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb{ patch_vbo_.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib{ patch_ibo_.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    QuadVertUBO vubo{};
    memcpy(vubo.vp, vp16, 64);
    vubo.origin_x       = origin_x;
    vubo.origin_z       = origin_z;
    vubo.size_m         = size_m;
    vubo.height_min_m   = height_min_m;
    vubo.height_max_m   = height_max_m;
    vubo.region_origin_x = region_origin_x_;
    vubo.region_origin_z = region_origin_z_;
    vubo.region_size     = region_size_;
    vubo.morph_t        = morph_t;
    vubo.grid_n         = (float)kPatchN;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    SDL_GPUTextureSamplerBinding vtsb{ height_tex_, height_sampler_ };
    SDL_BindGPUVertexSamplers(rp, 0, &vtsb, 1);

    SDL_DrawGPUIndexedPrimitives(rp, patch_idx_count_, 1, 0, 0, 0);
}
#endif // MD_SDL_GPU
