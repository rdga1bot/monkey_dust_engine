#pragma once
// ClutterRenderer — draws one chunk's baked ground-clutter mesh in ONE draw
// call, regardless of how many pebbles/small rocks/small plants are merged
// into it (see clutter_gen.h for the bake step). Mirrors PropRenderer's
// pipeline setup but has no per-instance uniform data at all — the chunk's
// clutter_vbo IS the mesh, already in world-space.

#include <monkey_dust/world/terrain_chunk.h>
#include <monkey_dust/render/gpu_hal.h>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

class ClutterRenderer {
public:
    bool Init();
    void Shutdown();
    bool IsReady() const { return ready_; }

    // vp16:  column-major mat4 (64 bytes).
    // sun32: TerrainRenderer::SunParams / PropFrag UBO (32 bytes: sun_dir_str + ambient).
    void DrawChunk(
#ifdef MD_SDL_GPU
        SDL_GPURenderPass*    rp,
        SDL_GPUCommandBuffer* cmd,
#endif
        const TerrainChunk& chunk,
        const float* vp16,
        const float* sun32);

private:
    GpuPipeline pipeline_;
    bool        ready_ = false;
};
