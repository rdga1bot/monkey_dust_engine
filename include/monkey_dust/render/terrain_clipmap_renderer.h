#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_clipmap_cache.h>
#include <monkey_dust/world/terrain_clipmap_mesh.h>

// GEOCLIPMAP Phase 4: draws the shared clipmap mesh (engine/include/
// monkey_dust/world/terrain_clipmap_mesh.h) for one level per DrawLevel
// call, VTF-sampling that level's TerrainClipmapCache toroidal cache.
// Output contract IDENTICAL to TerrainPatchRenderer::DrawBatchGBuffer --
// same shaders/terrain_gbuffer_mini.frag, unmodified, same packed-normal
// RGBA32F target -- TerrainShadingProjected needs zero changes to accept
// this renderer's output (the whole point of Phase 4, see
// serene-pondering-teapot.md's "Output contract незмінний" decision).
//
// Only ONE static mesh (1 VBO + 2 IBOs: filled for level 0, ring for
// every other level) for ALL 8 levels, vs the old system's 8 separate
// per-tier meshes -- every level just draws with a different per-level
// UBO (origin/texel_size), not a different vertex/index buffer.
//
// Minimal-variant Part B (per-tile culling, serene-pondering-teapot.md):
// each level's mesh is generated tile-major (kTileQuads-sized tiles) so
// DrawLevel can issue one draw PER VISIBLE TILE (a sub-range of the same
// shared IBO), skipping tiles outside the camera frustum entirely --
// borrows the old TerrainPatchGrid::SelectVisible's per-AABB frustum
// test (git history, engine commit 65859fe, pre-Phase-8 deletion), not
// its patch/tier machinery.
class TerrainClipmapRenderer {
public:
    static constexpr int kTileQuads = 16; // TerrainClipmapCache::kMeshQuads(128) / 16 = 8x8 tiles/level

    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Caller loops level=0..TerrainClipmapCache::kNumLevels-1, one call
    // each, inside an already-open G-buffer render pass -- matches
    // TerrainPatchRenderer::DrawBatchGBuffer's established per-tier
    // draw-call precedent (TerrainShadingProjected::BeginGBufferPass).
    // frustum_planes: 4 side planes (left/right/top/bottom -- same
    // convention this codebase's frustum culling has always used, no far
    // plane; see MdCamera::FrustumPlanes), used for per-tile AABB culling.
    void DrawLevel(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int level,
                    const TerrainClipmapCache& cache, const float* vp16,
                    float cam_x, float cam_y, float cam_z,
                    float height_min_m, float height_max_m,
                    const float frustum_planes[16]);

private:
    GpuPipeline gbuffer_pipeline_;
    GpuStaticBuffer vbo_;
    GpuStaticBuffer filled_ibo_;
    GpuStaticBuffer ring_ibo_;
    uint32_t filled_index_count_ = 0;
    uint32_t ring_index_count_   = 0;
    // kMeshQuads(128)/kTileQuads(16) = 8x8 = 64 tiles, fixed -- no
    // std::vector (hot-path: DrawLevel iterates this every frame).
    static constexpr int kMaxTiles = 64;
    md::ClipmapTileRange tiles_[kMaxTiles] = {};
    int num_tiles_ = 0;
    bool ready_ = false;
};
#endif
