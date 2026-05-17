#pragma once
// PropRenderer — draws rock props on terrain.
// - Loads a GLB mesh once via Init(glb_path).
// - Draws up to MAX_PROPS instances per frame (no per-frame malloc).
// - If GLB is missing / null, DrawRaw is a no-op.

#include <monkey_dust/render/prop_mesh.h>
#include <monkey_dust/render/gpu_hal.h>
#include <cstdint>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

class PropRenderer {
public:
    static constexpr int   MAX_PROPS = 64;
    static constexpr float SCALE     = 1.5f;  // rock scale in metres

    // Init: load GLB mesh + create pipeline.
    // path=nullptr → no-op, returns false (graceful for missing assets).
    bool Init(const char* glb_path);
    void Shutdown();

    bool IsReady() const { return mesh_.loaded; }

    // Draw N instances inside an already-open render pass.
    // positions_xyz: float array [count * 3], each triple is (x, y, z) world pos.
    // vp16:   column-major mat4 (64 bytes).
    // sun32:  TerrainRenderer::SunParams (32 bytes: vec4 sun_dir_str + vec4 ambient).
    void DrawRaw(
#ifdef MD_SDL_GPU
        SDL_GPURenderPass*    rp,
        SDL_GPUCommandBuffer* cmd,
#endif
        const float* positions_xyz,
        int          count,
        const float* vp16,
        const float* sun32);

private:
    PropMesh    mesh_;
    GpuPipeline pipeline_;
};
