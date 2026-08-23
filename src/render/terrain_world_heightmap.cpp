#include <monkey_dust/render/terrain_world_heightmap.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/world/chunk_def.h>
#include <monkey_dust/world/terrain_chunk.h>  // TERRAIN_GRID
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace {
// Field order matches shaders/terrain_worldmap_normal_bake.comp's
// NormalBakeUBO exactly (std140).
struct NormalBakeUBO {
    float world_params[4]; // x=worldExtent_m, y=resTexels, z=heightRange_m, w=heightMin_m
};
// Kenshi's real world is a fixed 64x64 zone grid (documented in
// terrain_gen.h's own "Terrain Atlas API" comment) -- not derived from
// any runtime state, safe to hardcode same as that header already does.
constexpr int kWorldZones = 64;

// Maps a world-grid sample index (0..kWorldZones*TERRAIN_GRID) to (zone
// index, local col/row 0..TERRAIN_GRID) -- zones share their boundary
// vertex (zone Z's col=TERRAIN_GRID == zone Z+1's col=0), so only the
// very last global sample needs the "clamp into the last zone's last
// col" special case. Same convention TerrainQuadtreeRenderer::
// s_sample_and_pack (terrain_quadtree_renderer.cpp) already uses for a
// windowed region, generalised here to the whole world at once.
void SampleIndexToZone(int gi, int& zi, int& local) {
    zi    = gi / TERRAIN_GRID;
    local = gi % TERRAIN_GRID;
    if (zi >= kWorldZones) { zi = kWorldZones - 1; local = TERRAIN_GRID; }
}
} // namespace

bool TerrainWorldHeightmap::Init(SDL_GPUDevice* dev) {
    if (!TerrainAtlas_Loaded()) {
        fprintf(stderr, "[TerrainWorldHeightmap] TerrainAtlas not loaded\n");
        return false;
    }

    const int N = kWorldZones * TERRAIN_GRID + 1; // 8193
    world_extent_ = (float)kWorldZones * CHUNK_SIZE;
    res_ = N;

    // Transient CPU buffers -- freed after upload, not stored on the
    // instance (unlike the old windowed system, this only ever runs
    // once, so there is no need to keep scratch storage resident).
    float* h_tmp = (float*)malloc(sizeof(float) * (size_t)N * (size_t)N);
    if (!h_tmp) {
        fprintf(stderr, "[TerrainWorldHeightmap] h_tmp alloc failed (%dx%d)\n", N, N);
        return false;
    }

    float hmin = 1e9f, hmax = -1e9f;
    for (int row = 0; row < N; ++row) {
        int zzi, lrow; SampleIndexToZone(row, zzi, lrow);
        for (int col = 0; col < N; ++col) {
            int zxi, lcol; SampleIndexToZone(col, zxi, lcol);
            float h = TerrainAtlas_GetHeight(zxi, zzi, lcol, lrow);
            h_tmp[(size_t)row * N + col] = h;
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }
    float range = hmax - hmin;
    if (range < 1.f) range = 1.f;
    height_min_ = hmin;
    height_max_ = hmin + range;

    // Pack as raw uint16 -- R16_UNORM, direct/lossless, no byte-split
    // packing (see header doc comment for why this differs from the old
    // windowed system's RGBA8 R+G-byte hack).
    uint16_t* h16 = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)N * (size_t)N);
    if (!h16) {
        free(h_tmp);
        fprintf(stderr, "[TerrainWorldHeightmap] h16 alloc failed (%dx%d)\n", N, N);
        return false;
    }
    for (size_t i = 0; i < (size_t)N * (size_t)N; ++i) {
        float norm = (h_tmp[i] - hmin) / range;
        uint32_t v = (uint32_t)(norm * 65535.f + 0.5f);
        if (v > 65535u) v = 65535u;
        h16[i] = (uint16_t)v;
    }
    free(h_tmp);

    SDL_GPUTextureCreateInfo ti = {};
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.width                = (Uint32)N;
    ti.height               = (Uint32)N;
    ti.layer_count_or_depth = 1;
    // #398 mip-chain removal (2026-08-23): this texture is ALWAYS sampled
    // at explicit textureLod(..., 0.0) in the live vertex shader path
    // (terrain_quadtree.vert's SampleHeightFrac/SampleNormal) -- geomorph's
    // "coarse" sample is a resample at a DIFFERENT WORLD POSITION on this
    // same LOD 0, not a different mip level. The one reader that COULD use
    // mip>0 (TerrainVtPageCache's page-fill compute, via TH_SampleHeightFrac's
    // explicit lod param) is provably dead in every exercised path: the game
    // never calls RequestPage/FlushFillQueue at all (VT cache disabled
    // 2026-08-09, see scene_render.h's terrain_vt_cache doc comment), and
    // the editor's only call site (tools/editor/editor_world_3d_sdlgpu.cpp's
    // VtDebugFill(), reachable only via a manual Lua console command) hardcodes
    // tier=0 (LOD 0) unconditionally. Full mip chain was previously
    // floor(log2(N))+1 (14 levels for N=8193) -- ~44MB of never-sampled
    // GPU memory. If VT paging is ever revived with tier>0, restore that
    // formula here (this texture/sampler is the same one page-fill binds).
    ti.num_levels = 1;
    ti.format     = SDL_GPU_TEXTUREFORMAT_R16_UNORM;
    ti.usage      = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    // COLOR_TARGET usage is required by SDL_GenerateMipmapsForGPUTexture
    // even though this texture is never bound as an actual pipeline
    // color attachment anywhere -- matches GpuTexture::InitFromMemory's
    // own gen_mipmap path (gpu_hal_buffers.cpp), same requirement there.

    tex_ = SDL_CreateGPUTexture(dev, &ti);
    if (!tex_) {
        fprintf(stderr, "[TerrainWorldHeightmap] SDL_CreateGPUTexture(R16_UNORM, %dx%d) failed: %s\n",
                N, N, SDL_GetError());
        free(h16);
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size  = (Uint32)((size_t)N * (size_t)N * sizeof(uint16_t));
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    if (!tb) {
        fprintf(stderr, "[TerrainWorldHeightmap] transfer buffer create failed: %s\n", SDL_GetError());
        free(h16);
        SDL_ReleaseGPUTexture(dev, tex_);
        tex_ = nullptr;
        return false;
    }
    void* ptr = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (ptr) memcpy(ptr, h16, (size_t)N * (size_t)N * sizeof(uint16_t));
    SDL_UnmapGPUTransferBuffer(dev, tb);
    free(h16);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.pixels_per_row   = (Uint32)N;
    src.rows_per_layer   = (Uint32)N;
    SDL_GPUTextureRegion dst{};
    dst.texture = tex_;
    dst.w = (Uint32)N; dst.h = (Uint32)N; dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    // #398: no SDL_GenerateMipmapsForGPUTexture call -- single level only,
    // see ti.num_levels=1's doc comment above.
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter        = SDL_GPU_FILTER_LINEAR;
    si.mag_filter        = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;  // moot with 1 level, harmless
    si.address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.min_lod           = 0.f;
    si.max_lod           = (float)(ti.num_levels - 1);
    sampler_ = SDL_CreateGPUSampler(dev, &si);
    if (!sampler_) {
        fprintf(stderr, "[TerrainWorldHeightmap] sampler create failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUTexture(dev, tex_);
        tex_ = nullptr;
        return false;
    }

    fprintf(stderr, "[TerrainWorldHeightmap] ready: %dx%d R16_UNORM, %u mips, height=[%.2f,%.2f]m, extent=%.1fm\n",
            N, N, ti.num_levels, height_min_, height_max_, world_extent_);

    // Full-variant Phase 3 (serene-pondering-teapot.md): bake the
    // world-wide normal texture now, once, right after the height texture
    // is ready -- same "one-time startup cost" class as everything above,
    // not a per-frame or per-window operation.
    {
        SDL_GPUTextureCreateInfo nti = {};
        nti.type                 = SDL_GPU_TEXTURETYPE_2D;
        nti.width                = (Uint32)N;
        nti.height               = (Uint32)N;
        nti.layer_count_or_depth = 1;
        nti.num_levels           = 1;
        nti.format               = SDL_GPU_TEXTUREFORMAT_R8G8_SNORM;
        nti.usage                = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE
                                  | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        normal_tex_ = SDL_CreateGPUTexture(dev, &nti);
        if (!normal_tex_) {
            fprintf(stderr, "[TerrainWorldHeightmap] normal texture create failed: %s\n", SDL_GetError());
            SDL_ReleaseGPUSampler(dev, sampler_); sampler_ = nullptr;
            SDL_ReleaseGPUTexture(dev, tex_); tex_ = nullptr;
            return false;
        }

        SDL_GPUSamplerCreateInfo nsi{};
        nsi.min_filter     = SDL_GPU_FILTER_LINEAR;
        nsi.mag_filter     = SDL_GPU_FILTER_LINEAR;
        nsi.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        nsi.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        nsi.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        nsi.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        normal_sampler_ = SDL_CreateGPUSampler(dev, &nsi);
        if (!normal_sampler_) {
            fprintf(stderr, "[TerrainWorldHeightmap] normal sampler create failed: %s\n", SDL_GetError());
            SDL_ReleaseGPUTexture(dev, normal_tex_); normal_tex_ = nullptr;
            SDL_ReleaseGPUSampler(dev, sampler_); sampler_ = nullptr;
            SDL_ReleaseGPUTexture(dev, tex_); tex_ = nullptr;
            return false;
        }

        GpuComputePipeline bake_pipeline;
        GpuComputePipeline::Desc pd{};
        pd.glsl_path           = "shaders/terrain_worldmap_normal_bake.comp";
        pd.num_uniform_buffers = 1;
        pd.num_samplers        = 1;
        pd.num_readwrite_storage_textures = 1;
        pd.threadcount_x = 8; pd.threadcount_y = 8; pd.threadcount_z = 1;
        if (!bake_pipeline.Create(pd)) {
            fprintf(stderr, "[TerrainWorldHeightmap] normal bake compute pipeline create failed\n");
            SDL_ReleaseGPUSampler(dev, normal_sampler_); normal_sampler_ = nullptr;
            SDL_ReleaseGPUTexture(dev, normal_tex_); normal_tex_ = nullptr;
            SDL_ReleaseGPUSampler(dev, sampler_); sampler_ = nullptr;
            SDL_ReleaseGPUTexture(dev, tex_); tex_ = nullptr;
            return false;
        }

        SDL_GPUCommandBuffer* bcmd = SDL_AcquireGPUCommandBuffer(dev);
        SDL_GPUStorageTextureReadWriteBinding rw{};
        rw.texture = normal_tex_;
        rw.cycle   = false;
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(bcmd, &rw, 1, nullptr, 0);
        if (pass) {
            SDL_BindGPUComputePipeline(pass, bake_pipeline.SDLComputePipeline());
            SDL_GPUTextureSamplerBinding samp{};
            samp.texture = tex_;
            samp.sampler = sampler_;
            SDL_BindGPUComputeSamplers(pass, 0, &samp, 1);

            NormalBakeUBO nubo{};
            nubo.world_params[0] = world_extent_;
            nubo.world_params[1] = (float)N;
            nubo.world_params[2] = height_max_ - height_min_;
            nubo.world_params[3] = height_min_;
            SDL_PushGPUComputeUniformData(bcmd, 0, &nubo, sizeof(nubo));

            uint32_t g = (uint32_t)((N + 7) / 8);
            SDL_DispatchGPUCompute(pass, g, g, 1);
            SDL_EndGPUComputePass(pass);
        } else {
            fprintf(stderr, "[TerrainWorldHeightmap] normal bake SDL_BeginGPUComputePass failed: %s\n", SDL_GetError());
        }
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(bcmd);
        if (fence) { SDL_WaitForGPUFences(dev, true, &fence, 1); SDL_ReleaseGPUFence(dev, fence); }
        bake_pipeline.Destroy();

        fprintf(stderr, "[TerrainWorldHeightmap] normal bake done: %dx%d RG8_SNORM\n", N, N);
    }

    return true;
}

void TerrainWorldHeightmap::Shutdown(SDL_GPUDevice* dev) {
    if (normal_sampler_) { SDL_ReleaseGPUSampler(dev, normal_sampler_); normal_sampler_ = nullptr; }
    if (normal_tex_)     { SDL_ReleaseGPUTexture(dev, normal_tex_);     normal_tex_     = nullptr; }
    if (sampler_) { SDL_ReleaseGPUSampler(dev, sampler_); sampler_ = nullptr; }
    if (tex_)     { SDL_ReleaseGPUTexture(dev, tex_);     tex_     = nullptr; }
}
#endif
