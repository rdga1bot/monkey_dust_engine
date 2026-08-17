#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

class TerrainWorldHeightmap;

// GEOCLIPMAP (serene-pondering-teapot.md, Phase 3) -- production toroidal
// height-cache manager for one Geometry Clipmapping level set. Graduates
// the exhaustively-tested CPU addressing logic from
// test_terrain_clipmap_seam.cpp (0 boundary mismatches across all 8
// levels / 7 adjacent pairs, many camera moves including multi-period
// wraparound, real bug found+fixed there: toroidal address is idx MOD
// cacheSize, never idx-origin) into a reusable engine class, sampling the
// REAL TerrainWorldHeightmap via shaders/terrain_clipmap_fill.comp
// instead of that test's synthetic R32_UINT source.
//
// kNumLevels=8, kMeshQuads=128, kBaseTexelSize=300/128≈2.34m are the
// plan's stated starting hypothesis (reusing TerrainPatchRenderer's own
// kNumTiers=8/kPatchN=128/patch_size=300m precedent) -- tune empirically
// once a real frame-time measurement exists (Phase 3's remaining "tune
// N/L against real budget" item), not locked in.
//
// kCacheTexels = kMeshQuads+1, NOT kMeshQuads -- a real design bug caught
// during Phase 4 shader design, before it shipped: BuildClipmapMesh(N)
// produces N+1 vertices (0..N inclusive, matching TerrainPatchRenderer's
// own vertex-grid convention). If the cache only had N texels, vertex N
// (idx = origin_index+N) and vertex 0 (idx = origin_index) would both
// hash to the SAME texel via `idx mod N` (any multiple of N reduces to
// 0), so the mesh's far edge would silently duplicate its near edge's
// height instead of sampling its own true world position -- a real crack
// at every level's outer boundary, exactly the class of bug the whole
// Phase 2 seam-invariant exercise exists to catch. Sizing the cache to
// N+1 texels makes vertex idx range [origin_index, origin_index+N]
// (N+1 distinct values) map to N+1 distinct texels with zero collision.
//
// Each level's cache is a small (129x129) R32_FLOAT storage-writable
// texture holding the raw [0,1] height FRACTION (same encoding
// TerrainWorldHeightmap itself uses) -- height_min/max decode into an
// absolute world-Y happens once in the consuming vertex shader (Phase 4),
// not per-fill here. Reads use hardware SDL_GPU_SAMPLERADDRESSMODE_REPEAT
// (the toroidal wrap is free on the GPU's own wrap-mode hardware) --
// deliberately NOT an indirection-texture+slot-table design like
// TerrainVtPageCache's, since clipmap addressing is deterministic
// (idx mod cacheSize), not an LRU pool of independently-placed pages.
class TerrainClipmapCache {
public:
    static constexpr int kNumLevels     = 8;
    static constexpr int kMeshQuads     = 128; // matches BuildClipmapMesh(N)'s N
    static constexpr int kCacheTexels   = kMeshQuads + 1; // 129 -- see class doc comment
    static constexpr float kBaseTexelSize = 300.0f / 128.0f; // level 0, ~2.34m/texel

    bool Init(SDL_GPUDevice* dev, const TerrainWorldHeightmap& hmap);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Recenters ALL kNumLevels levels around world position (cam_x, cam_z),
    // issuing only the necessary edge-strip fill dispatches (a full fill
    // only on the very first call after Init). Must run outside any
    // render/compute pass already open by the caller -- this opens and
    // closes its own compute passes per dispatch (matching
    // TerrainVtPageCache::FlushFillQueue's established precedent for
    // storage-texture writes on this HAL).
    void Recenter(SDL_GPUCommandBuffer* cmd, float cam_x, float cam_z);

    SDL_GPUTexture* CacheTexture(int level) const { return levels_[level].tex; }
    SDL_GPUSampler* CacheSampler(int level) const { return levels_[level].sampler; }
    float TexelSize(int level) const;
    // World-space origin (metres) of this level's current window --
    // level_origin_world = origin_index * TexelSize(level).
    void LevelOriginWorld(int level, float& out_x, float& out_z) const;
    // Raw INDEX-space origin (integer texel-index units, not metres) --
    // the consuming vertex shader needs this directly, not
    // origin_world/TexelSize(level) recomputed client-side: that
    // division isn't guaranteed bit-exact in float32 (non-associative
    // rounding), which would misalign the shader's recovered integer
    // index by a sub-texel epsilon against what the fill shader actually
    // wrote to -- silently breaking the exact texel-center VTF sampling
    // this whole cache design depends on.
    void LevelOriginIndex(int level, int& out_x, int& out_z) const {
        out_x = levels_[level].origin_index[0];
        out_z = levels_[level].origin_index[1];
    }

private:
    struct Level {
        SDL_GPUTexture* tex     = nullptr;
        SDL_GPUSampler* sampler = nullptr;
        int origin_index[2]     = {0, 0};
    };

    void DispatchFill(SDL_GPUCommandBuffer* cmd, int level, int ox, int oz, int sw, int sh);
    void RecenterLevel(SDL_GPUCommandBuffer* cmd, int level, int new_ox, int new_oz);

    Level levels_[kNumLevels];
    GpuComputePipeline fill_pipeline_;
    SDL_GPUTexture* height_tex_     = nullptr; // borrowed from TerrainWorldHeightmap, not owned
    SDL_GPUSampler* height_sampler_ = nullptr; // borrowed
    float world_extent_ = 0.f;
    float res_texels_   = 0.f;
    bool  ready_         = false;
    bool  first_recenter_ = true;
};
#endif
