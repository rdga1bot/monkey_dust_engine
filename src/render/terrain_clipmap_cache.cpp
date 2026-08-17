#include <monkey_dust/render/terrain_clipmap_cache.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/terrain_world_heightmap.h>
#include <monkey_dust/platform/md_log.h>
#include <cmath>
#include <cstdlib>

namespace {
// Field order matches shaders/terrain_clipmap_fill.comp's ClipmapFillUBO
// exactly (std140) -- every vec4-sized block here maps 1:1 to a vec4 in
// the GLSL struct, INCLUDING the padding lanes in normal_params (std140
// lesson from terrain_page_fill.comp: verify size/order by hand on both
// sides, not just "add a field and hope").
struct ClipmapFillUBO {
    int32_t fill_origin_index_and_strip_size[4];
    float   world_params[4];
    float   normal_params[4]; // x = height_range_m (yzw unused/pad)
};
} // namespace

bool TerrainClipmapCache::Init(SDL_GPUDevice* dev, const TerrainWorldHeightmap& hmap) {
    if (!dev || !hmap.IsReady()) {
        MD_LOG(MD_LOG_WARNING, "[TerrainClipmapCache] Init: invalid device or heightmap not ready");
        return false;
    }
    height_tex_     = hmap.Texture();
    height_sampler_ = hmap.Sampler();
    world_extent_   = hmap.WorldExtent();
    res_texels_     = (float)hmap.Resolution();
    height_range_   = hmap.HeightMax() - hmap.HeightMin();

    for (int lvl = 0; lvl < kNumLevels; ++lvl) {
        SDL_GPUTextureCreateInfo ti{};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.width                = kCacheTexels;
        ti.height               = kCacheTexels;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE
                                 | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ
                                 | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        levels_[lvl].tex = SDL_CreateGPUTexture(dev, &ti);
        if (!levels_[lvl].tex) {
            MD_LOG(MD_LOG_WARNING, "[TerrainClipmapCache] level %d cache texture create failed: %s", lvl, SDL_GetError());
            Shutdown(dev);
            return false;
        }

        SDL_GPUSamplerCreateInfo si{};
        si.min_filter     = SDL_GPU_FILTER_LINEAR;
        si.mag_filter     = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        // Toroidal wrap is free on the GPU's own address-mode hardware --
        // deterministic (idx mod cacheSize) addressing, unlike
        // TerrainVtPageCache's LRU slot-table which needed an indirection
        // texture precisely because ITS addressing isn't deterministic.
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        levels_[lvl].sampler = SDL_CreateGPUSampler(dev, &si);
        if (!levels_[lvl].sampler) {
            MD_LOG(MD_LOG_WARNING, "[TerrainClipmapCache] level %d cache sampler create failed: %s", lvl, SDL_GetError());
            Shutdown(dev);
            return false;
        }
    }

    GpuComputePipeline::Desc pd{};
    pd.glsl_path              = "shaders/terrain_clipmap_fill.comp";
    pd.num_uniform_buffers    = 1;
    pd.num_samplers           = 1;
    pd.num_readwrite_storage_textures = 1;
    pd.threadcount_x = 8; pd.threadcount_y = 8; pd.threadcount_z = 1;
    if (!fill_pipeline_.Create(pd)) {
        MD_LOG(MD_LOG_WARNING, "[TerrainClipmapCache] fill compute pipeline create failed");
        Shutdown(dev);
        return false;
    }

    first_recenter_ = true;
    ready_ = true;
    return true;
}

void TerrainClipmapCache::Shutdown(SDL_GPUDevice* dev) {
    if (!dev) return;
    fill_pipeline_.Destroy();
    for (int lvl = 0; lvl < kNumLevels; ++lvl) {
        if (levels_[lvl].tex) { SDL_ReleaseGPUTexture(dev, levels_[lvl].tex); levels_[lvl].tex = nullptr; }
        if (levels_[lvl].sampler) { SDL_ReleaseGPUSampler(dev, levels_[lvl].sampler); levels_[lvl].sampler = nullptr; }
    }
    ready_ = false;
}

float TerrainClipmapCache::TexelSize(int level) const {
    return kBaseTexelSize * (float)(1 << level);
}

void TerrainClipmapCache::LevelOriginWorld(int level, float& out_x, float& out_z) const {
    float ts = TexelSize(level);
    out_x = (float)levels_[level].origin_index[0] * ts;
    out_z = (float)levels_[level].origin_index[1] * ts;
}

void TerrainClipmapCache::DispatchFill(SDL_GPUCommandBuffer* cmd, int level, int ox, int oz, int sw, int sh) {
    if (sw <= 0 || sh <= 0) return;

    SDL_GPUStorageTextureReadWriteBinding rw{};
    rw.texture = levels_[level].tex;
    rw.cycle   = false;
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "[TerrainClipmapCache] SDL_BeginGPUComputePass failed: %s", SDL_GetError());
        return;
    }
    SDL_BindGPUComputePipeline(pass, fill_pipeline_.SDLComputePipeline());

    SDL_GPUTextureSamplerBinding samp{};
    samp.texture = height_tex_;
    samp.sampler = height_sampler_;
    SDL_BindGPUComputeSamplers(pass, 0, &samp, 1);

    ClipmapFillUBO ubo{};
    ubo.fill_origin_index_and_strip_size[0] = ox;
    ubo.fill_origin_index_and_strip_size[1] = oz;
    ubo.fill_origin_index_and_strip_size[2] = sw;
    ubo.fill_origin_index_and_strip_size[3] = sh;
    ubo.world_params[0] = TexelSize(level);
    ubo.world_params[1] = (float)kCacheTexels;
    ubo.world_params[2] = world_extent_;
    ubo.world_params[3] = res_texels_;
    ubo.normal_params[0] = height_range_;
    ubo.normal_params[1] = 0.f;
    ubo.normal_params[2] = 0.f;
    ubo.normal_params[3] = 0.f;
    SDL_PushGPUComputeUniformData(cmd, 0, &ubo, sizeof(ubo));

    uint32_t gx = (uint32_t)((sw + 7) / 8);
    uint32_t gy = (uint32_t)((sh + 7) / 8);
    SDL_DispatchGPUCompute(pass, gx, gy, 1);
    SDL_EndGPUComputePass(pass);
}

void TerrainClipmapCache::RecenterLevel(SDL_GPUCommandBuffer* cmd, int level, int new_ox, int new_oz) {
    int prev_ox = levels_[level].origin_index[0];
    int prev_oz = levels_[level].origin_index[1];
    int dx = new_ox - prev_ox;
    int dz = new_oz - prev_oz;
    if (std::abs(dx) > kCacheTexels || std::abs(dz) > kCacheTexels) {
        // Moved further than the window itself in one step (teleport/fast
        // fly-through, or a very first call bug) -- incremental strip
        // logic doesn't apply, do a full refill instead of silently
        // leaving stale/garbage texels resident.
        DispatchFill(cmd, level, new_ox, new_oz, kCacheTexels, kCacheTexels);
        levels_[level].origin_index[0] = new_ox;
        levels_[level].origin_index[1] = new_oz;
        return;
    }
    if (dx != 0) {
        int strip_ox = dx > 0 ? (prev_ox + kCacheTexels) : new_ox;
        DispatchFill(cmd, level, strip_ox, new_oz, std::abs(dx), kCacheTexels);
    }
    if (dz != 0) {
        int strip_oz = dz > 0 ? (prev_oz + kCacheTexels) : new_oz;
        DispatchFill(cmd, level, new_ox, strip_oz, kCacheTexels, std::abs(dz));
    }
    levels_[level].origin_index[0] = new_ox;
    levels_[level].origin_index[1] = new_oz;
}

void TerrainClipmapCache::Recenter(SDL_GPUCommandBuffer* cmd, float cam_x, float cam_z) {
    if (!ready_) return;
    // Each level's origin is snapped independently from raw camera
    // position -- this does NOT make a coarser level's fixed ring-hole
    // land exactly on the next-finer level's actual footprint (the two
    // levels' texel grids generally don't share a common origin modulo
    // 2, off by 0 or 1 finer-texel depending on camera sub-texel
    // position -- a standard floor(x)-2*floor(x/2) parity identity, found
    // 2026-08-16 via "square rings around camera" at aerial altitude,
    // where the affected coarse levels' texel size is large enough to
    // make a 1-texel gap glaring). A chained derivation (each origin
    // computed exactly from the next-finer one) was tried and rejected:
    // it can only make ONE pair exact without forcing level 0's origin to
    // a multiple of 2^(kNumLevels-1), which would push level 0's ~300m
    // window off-camera entirely. The actual fix is at the mesh level --
    // terrain_clipmap_mesh.cpp's ring hole carries a deliberate 1-quad
    // margin on every edge specifically to absorb this bounded
    // worst-case mismatch as overlap (z-fighting-safe) instead of a gap.
    for (int lvl = 0; lvl < kNumLevels; ++lvl) {
        float ts = TexelSize(lvl);
        float half_world = (kCacheTexels * ts) * 0.5f;
        int new_ox = (int)std::floor((cam_x - half_world) / ts);
        int new_oz = (int)std::floor((cam_z - half_world) / ts);
        if (first_recenter_) {
            DispatchFill(cmd, lvl, new_ox, new_oz, kCacheTexels, kCacheTexels);
            levels_[lvl].origin_index[0] = new_ox;
            levels_[lvl].origin_index[1] = new_oz;
        } else {
            RecenterLevel(cmd, lvl, new_ox, new_oz);
        }
    }
    first_recenter_ = false;
}
#endif
