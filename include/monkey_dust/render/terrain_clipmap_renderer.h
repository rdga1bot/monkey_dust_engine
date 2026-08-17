#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_clipmap_cache.h>

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
class TerrainClipmapRenderer {
public:
    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Caller loops level=0..TerrainClipmapCache::kNumLevels-1, one call
    // each, inside an already-open G-buffer render pass -- matches
    // TerrainPatchRenderer::DrawBatchGBuffer's established per-tier
    // draw-call precedent (TerrainShadingProjected::BeginGBufferPass).
    void DrawLevel(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int level,
                    const TerrainClipmapCache& cache, const float* vp16,
                    float cam_x, float cam_y, float cam_z,
                    float height_min_m, float height_max_m);

private:
    GpuPipeline gbuffer_pipeline_;
    GpuStaticBuffer vbo_;
    GpuStaticBuffer filled_ibo_;
    GpuStaticBuffer ring_ibo_;
    uint32_t filled_index_count_ = 0;
    uint32_t ring_index_count_   = 0;
    bool ready_ = false;
};
#endif
