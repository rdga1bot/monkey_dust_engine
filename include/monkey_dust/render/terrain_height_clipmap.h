#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

class TerrainWorldHeightmap;

// #398 (2026-08-23): toroidal height+normal cache, one level per
// TerrainQuadtree depth (0..3, TerrainQuadtree::kMaxDepth), replacing
// TerrainWorldHeightmap's world-wide R16_UNORM+R8G8_SNORM textures
// (~268-312MB resident, mostly unread -- see terrain_world_heightmap.cpp's
// #398 doc comment on the now-removed height mip chain) as the source
// TerrainQuadtreeRenderer::DrawNode binds per-draw.
//
// Design directly adapts the proven, exhaustively-documented GEOCLIPMAP
// toroidal cache (deleted 2026-08-18 when Ogre-quadtree replaced that
// whole GEOMETRY system -- `git show 0b5b312~1:include/monkey_dust/
// render/terrain_clipmap_cache.h` in this submodule's history): same
// idx-mod-cacheSize toroidal addressing (0 boundary mismatches, proven
// in that system's own seam test), same N+1 texel sizing (avoids a
// wrap-collision crack at the window's own far edge -- see that class's
// doc comment for the exact off-by-one bug this guards), same hardware
// SDL_GPU_SAMPLERADDRESSMODE_REPEAT wrap, same combined RGBA32F texture
// (r=height FRACTION [0,1] same encoding TerrainWorldHeightmap uses,
// g/b=baked normal.xz, normal.y reconstructed downstream via
// sqrt(1-x^2-z^2)), same edge-strip-only Recenter().
//
// What's DIFFERENT from the deleted 8-level system, and why: that system
// fed a GEOMETRY clipmap (fixed mesh, camera-relative), needing many
// levels of decreasing resolution to cover distance cheaply. This system
// feeds TerrainQuadtree's EXISTING geometry (unchanged) -- the quadtree's
// own recursion already determines which depth (0=coarsest..3=finest) a
// visible node belongs to, and `TerrainQuadtreeRenderer::DrawNode`
// already derives that node's own texelSize from its depth
// (CHUNK_SIZE/16/2^depth = 28.8/14.4/7.2/3.6m). So this cache needs
// EXACTLY 4 levels (one per depth, not 8), indexed directly by
// node.depth -- no separate "which level for this distance" mapping.
//
// Per-level real-world coverage need (derived from RecurseNode's own
// `range = size * detail_multiplier(3.0)` subdivide threshold -- a
// depth-D node is only ever DRAWN, not further subdivided, roughly
// beyond that depth's own range, out to kMaxRenderDistance+size at
// depth 0): depth 0 ~1382-4460m, depth 1 ~691-1382m, depth 2 ~346-691m,
// depth 3 ~0-346m. A UNIFORM kCacheTexels across all 4 levels (like the
// deleted system used across its 8) already self-scales coverage
// correctly, since texelSize halves each depth exactly as the needed
// radius halves -- kCacheTexels=321 gives every level a comfortable
// margin over its own real need (worst case, depth 0: 321*28.8m ≈
// 9245m diameter vs ~4460m radius needed) at ~6.6MB total (321²*16B*4
// levels), matching the task's own "~6MB working set" target.
class TerrainHeightClipmap {
public:
    static constexpr int kNumLevels   = 4;  // matches TerrainQuadtree::kMaxDepth+1
    static constexpr int kCacheTexels = 321; // N+1, N=320 quads -- see class doc comment
    // depth 0 (coarsest) = CHUNK_SIZE/16 = 28.8m; halves per depth.
    static constexpr float kDepth0TexelSize = 28.8f;

    bool Init(SDL_GPUDevice* dev, const TerrainWorldHeightmap& hmap);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Recenters all kNumLevels levels around world position (cam_x, cam_z).
    // Must run outside any render/compute pass already open by the
    // caller -- opens/closes its own compute passes per dispatch (same
    // constraint as the deleted system's own Recenter, and
    // TerrainVtPageCache::FlushFillQueue's established precedent).
    void Recenter(SDL_GPUCommandBuffer* cmd, float cam_x, float cam_z);

    SDL_GPUTexture* LevelTexture(int depth) const { return levels_[depth].tex; }
    SDL_GPUSampler* LevelSampler(int depth) const { return levels_[depth].sampler; }
    static float TexelSize(int depth) { return kDepth0TexelSize / (float)(1 << depth); }
    // Raw INDEX-space origin (integer texel-index units) -- the
    // consuming vertex shader needs this directly, not
    // origin_world/TexelSize(depth) recomputed client-side (float32
    // division isn't guaranteed bit-exact, would misalign the shader's
    // recovered index by a sub-texel epsilon against what the fill
    // shader actually wrote -- same lesson as the deleted system).
    void LevelOriginIndex(int depth, int& out_x, int& out_z) const {
        out_x = levels_[depth].origin_index[0];
        out_z = levels_[depth].origin_index[1];
    }

private:
    struct Level {
        SDL_GPUTexture* tex     = nullptr;
        SDL_GPUSampler* sampler = nullptr;
        int origin_index[2]     = {0, 0};
    };

    void DispatchFill(SDL_GPUCommandBuffer* cmd, int depth, int ox, int oz, int sw, int sh);
    void RecenterLevel(SDL_GPUCommandBuffer* cmd, int depth, int new_ox, int new_oz);

    Level levels_[kNumLevels];
    GpuComputePipeline fill_pipeline_;
    SDL_GPUTexture* height_tex_     = nullptr; // borrowed from TerrainWorldHeightmap, not owned
    SDL_GPUSampler* height_sampler_ = nullptr; // borrowed
    float world_extent_ = 0.f;
    float res_texels_   = 0.f;
    float height_range_ = 0.f; // hmap.HeightMax()-HeightMin(), fill shader needs it for baked-normal scale
    bool  ready_          = false;
    bool  first_recenter_ = true;
};
#endif
