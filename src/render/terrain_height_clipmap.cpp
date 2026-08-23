#include <monkey_dust/render/terrain_height_clipmap.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/terrain_world_heightmap.h>
#include <monkey_dust/platform/md_log.h>
#include <cmath>
#include <cstdlib>

namespace {
// Field order matches shaders/terrain_height_clipmap_fill.comp's
// ClipmapFillUBO exactly (std140) -- verified by hand on both sides,
// same lesson as terrain_page_fill.comp/the deleted terrain_clipmap_
// fill.comp: every vec4-sized block here maps 1:1 to a vec4 in GLSL.
struct ClipmapFillUBO {
    int32_t fill_origin_index_and_strip_size[4];
    float   world_params[4];  // x=texelSize, y=cacheTexels, z=world_extent, w=res_texels
    float   normal_params[4]; // x=height_range_m (yzw unused/pad)
};
} // namespace

bool TerrainHeightClipmap::Init(SDL_GPUDevice* dev, const TerrainWorldHeightmap& hmap) {
    if (!dev || !hmap.IsReady()) {
        MD_LOG(MD_LOG_WARNING, "[TerrainHeightClipmap] Init: invalid device or heightmap not ready");
        return false;
    }
    height_tex_     = hmap.Texture();
    height_sampler_ = hmap.Sampler();
    world_extent_   = hmap.WorldExtent();
    res_texels_     = (float)hmap.Resolution();
    height_range_   = hmap.HeightMax() - hmap.HeightMin();

    for (int depth = 0; depth < kNumLevels; ++depth) {
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
        levels_[depth].tex = SDL_CreateGPUTexture(dev, &ti);
        if (!levels_[depth].tex) {
            MD_LOG(MD_LOG_WARNING, "[TerrainHeightClipmap] depth %d cache texture create failed: %s", depth, SDL_GetError());
            Shutdown(dev);
            return false;
        }

        SDL_GPUSamplerCreateInfo si{};
        si.min_filter     = SDL_GPU_FILTER_LINEAR;
        si.mag_filter     = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        // Toroidal wrap free on GPU address-mode hardware -- deterministic
        // (idx mod cacheSize) addressing, same as the deleted GEOCLIPMAP
        // cache this design is adapted from.
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        levels_[depth].sampler = SDL_CreateGPUSampler(dev, &si);
        if (!levels_[depth].sampler) {
            MD_LOG(MD_LOG_WARNING, "[TerrainHeightClipmap] depth %d cache sampler create failed: %s", depth, SDL_GetError());
            Shutdown(dev);
            return false;
        }
    }

    GpuComputePipeline::Desc pd{};
    pd.glsl_path                       = "shaders/terrain_height_clipmap_fill.comp";
    pd.num_uniform_buffers             = 1;
    pd.num_samplers                    = 1;
    pd.num_readwrite_storage_textures  = 1;
    pd.threadcount_x = 8; pd.threadcount_y = 8; pd.threadcount_z = 1;
    if (!fill_pipeline_.Create(pd)) {
        MD_LOG(MD_LOG_WARNING, "[TerrainHeightClipmap] fill compute pipeline create failed");
        Shutdown(dev);
        return false;
    }

    first_recenter_ = true;
    ready_ = true;
    MD_LOG(MD_LOG_INFO, "[TerrainHeightClipmap] ready: %d levels x %dx%d RGBA32F (~%.1fMB total)",
           kNumLevels, kCacheTexels, kCacheTexels,
           (double)kNumLevels * kCacheTexels * kCacheTexels * 16.0 / (1024.0 * 1024.0));
    return true;
}

void TerrainHeightClipmap::Shutdown(SDL_GPUDevice* dev) {
    if (!dev) return;
    fill_pipeline_.Destroy();
    for (int depth = 0; depth < kNumLevels; ++depth) {
        if (levels_[depth].tex) { SDL_ReleaseGPUTexture(dev, levels_[depth].tex); levels_[depth].tex = nullptr; }
        if (levels_[depth].sampler) { SDL_ReleaseGPUSampler(dev, levels_[depth].sampler); levels_[depth].sampler = nullptr; }
    }
    ready_ = false;
}

void TerrainHeightClipmap::DispatchFill(SDL_GPUCommandBuffer* cmd, int depth, int ox, int oz, int sw, int sh) {
    if (sw <= 0 || sh <= 0) return;

    SDL_GPUStorageTextureReadWriteBinding rw{};
    rw.texture = levels_[depth].tex;
    rw.cycle   = false;
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "[TerrainHeightClipmap] SDL_BeginGPUComputePass failed: %s", SDL_GetError());
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
    ubo.world_params[0] = TexelSize(depth);
    ubo.world_params[1] = (float)kCacheTexels;
    ubo.world_params[2] = world_extent_;
    ubo.world_params[3] = res_texels_;
    ubo.normal_params[0] = height_range_;
    ubo.normal_params[1] = 0.f;
    ubo.normal_params[2] = 0.f;
    ubo.normal_params[3] = 0.f;
    SDL_PushGPUComputeUniformData(cmd, 0, &ubo, sizeof(ubo));

    auto gx = (uint32_t)((sw + 7) / 8);
    auto gy = (uint32_t)((sh + 7) / 8);
    SDL_DispatchGPUCompute(pass, gx, gy, 1);
    SDL_EndGPUComputePass(pass);
}

void TerrainHeightClipmap::RecenterLevel(SDL_GPUCommandBuffer* cmd, int depth, int new_ox, int new_oz) {
    int prev_ox = levels_[depth].origin_index[0];
    int prev_oz = levels_[depth].origin_index[1];
    int dx = new_ox - prev_ox;
    int dz = new_oz - prev_oz;
    if (std::abs(dx) > kCacheTexels || std::abs(dz) > kCacheTexels) {
        // Moved further than the window itself in one step (teleport/
        // fast fly-through) -- incremental strip logic doesn't apply,
        // full refill instead of leaving stale/garbage texels resident.
        DispatchFill(cmd, depth, new_ox, new_oz, kCacheTexels, kCacheTexels);
        levels_[depth].origin_index[0] = new_ox;
        levels_[depth].origin_index[1] = new_oz;
        return;
    }
    if (dx != 0) {
        int strip_ox = dx > 0 ? (prev_ox + kCacheTexels) : new_ox;
        DispatchFill(cmd, depth, strip_ox, new_oz, std::abs(dx), kCacheTexels);
    }
    if (dz != 0) {
        int strip_oz = dz > 0 ? (prev_oz + kCacheTexels) : new_oz;
        DispatchFill(cmd, depth, new_ox, strip_oz, kCacheTexels, std::abs(dz));
    }
    levels_[depth].origin_index[0] = new_ox;
    levels_[depth].origin_index[1] = new_oz;
}

void TerrainHeightClipmap::Recenter(SDL_GPUCommandBuffer* cmd, float cam_x, float cam_z) {
    if (!ready_) return;
    // Each level's origin snapped independently from raw camera position
    // -- same known, bounded floor()-parity mismatch between adjacent
    // levels as the deleted GEOCLIPMAP cache's own Recenter doc comment
    // describes (a standard floor(x)-2*floor(x/2) identity, not fixable
    // by chaining without pushing the coarsest level's window off-camera).
    // Not a correctness issue here (unlike the deleted system, this
    // cache doesn't drive GEOMETRY -- TerrainQuadtree's mesh is
    // unaffected by this texture-cache's window alignment), only a
    // possible sub-texel resample-position difference at a depth
    // boundary; the vertex shader's existing fine/coarse geomorph blend
    // already smooths any such seam the same way it already smooths
    // ordinary depth-boundary morphing.
    for (int depth = 0; depth < kNumLevels; ++depth) {
        float ts = TexelSize(depth);
        float half_world = ((float)kCacheTexels * ts) * 0.5f;
        int new_ox = (int)std::floor((cam_x - half_world) / ts);
        int new_oz = (int)std::floor((cam_z - half_world) / ts);
        if (first_recenter_) {
            DispatchFill(cmd, depth, new_ox, new_oz, kCacheTexels, kCacheTexels);
            levels_[depth].origin_index[0] = new_ox;
            levels_[depth].origin_index[1] = new_oz;
        } else {
            RecenterLevel(cmd, depth, new_ox, new_oz);
        }
    }
    first_recenter_ = false;
}
#endif
