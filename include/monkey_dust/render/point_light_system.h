#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

static constexpr int MAX_POINT_LIGHTS = 32;

struct PointLight {
    float pos[3];
    float radius;
    float color[3];
    float intensity;
};

namespace md {

// Deferred point light volumes — icosphere (80 tris) per light.
//
// Each light draws a world-space sphere (scale*translate × view_proj).
// Additive blend into swapchain; depth sampled from GBuffer as texture.
// Gated by RenderTierSystem::IsDeferred() — noop on Forward tier (HD 520).
//
// Usage:
//   Init(dev)           — once, after GpuDevice ready
//   AddLight(light)     — returns slot index or -1 if full
//   RemoveLight(idx)
//   Draw(cmd, ...)      — call after GBuffer::End(), before swapchain End
//   Shutdown()
class PointLightSystem {
public:
    static PointLightSystem& Get();

    void Init(SDL_GPUDevice* dev);
    void Shutdown();

    int  AddLight(const PointLight& light);  // -1 if MAX_POINT_LIGHTS reached
    void RemoveLight(int idx);
    void UpdateLight(int idx, const PointLight& light);
    void Clear();

    // Draws all active lights additively into swapchain_tex (LOAD).
    // gbuf_* textures come from GBuffer::RT0()/RT1()/Depth()/Sampler().
    // view_proj_16: 4×4 column-major float array (same convention as GBuffer pass).
    // inv_view_proj_16: inverse of view_proj_16 for depth reconstruction.
    void Draw(SDL_GPUCommandBuffer* cmd,
              SDL_GPUTexture*       gbuf_rt0,
              SDL_GPUTexture*       gbuf_rt1,
              SDL_GPUTexture*       gbuf_depth,
              SDL_GPUSampler*       gbuf_sampler,
              SDL_GPUTexture*       swapchain_tex,
              const float*          view_proj_16,
              const float*          inv_view_proj_16,
              const float*          cam_pos_3,
              int screen_w, int screen_h);

    bool IsEnabled() const { return enabled_; }
    int  Count()     const { return light_count_; }

private:
    PointLightSystem() = default;

    bool           enabled_           = false;
    SDL_GPUDevice* dev_               = nullptr;
    GpuPipeline    pipeline_;
    GpuStaticBuffer sphere_verts_;
    int            sphere_vert_count_ = 0;   // 240 (80 tris × 3 verts)

    PointLight lights_[MAX_POINT_LIGHTS] = {};
    bool       active_[MAX_POINT_LIGHTS] = {};
    int        light_count_              = 0;
};

} // namespace md
#endif // MD_SDL_GPU
