#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

static constexpr int MAX_STRIP_LIGHTS = 16;

struct StripLight {
    float p0[3];       // start endpoint (world space)
    float p1[3];       // end endpoint (world space)
    float radius;      // capsule radius (metres)
    float color[3];
    float intensity;
};

namespace md {

// Deferred strip (capsule) lights — AI DEFERREDSTRIPLIGHTING analog.
// Each strip is drawn as an AABB bounding the capsule volume; the fragment
// shader evaluates the capsule SDF for per-pixel occlusion + attenuation.
// Additive blend into swapchain; depth sampled from GBuffer texture.
// Gated by RenderTierSystem::IsDeferred().
//
// Usage:
//   Init(dev)
//   AddLight(strip) → idx; RemoveLight(idx); UpdateLight(idx, strip)
//   Draw(cmd, gbuf_rt0, gbuf_rt1, gbuf_depth, gbuf_sampler, swapchain_tex,
//        view_proj_16, inv_view_proj_16, cam_pos_3, w, h)
class StripLightSystem {
public:
    static StripLightSystem& Get();

    void Init(SDL_GPUDevice* dev);
    void Shutdown();

    int  AddLight(const StripLight& light);  // -1 if full
    void RemoveLight(int idx);
    void UpdateLight(int idx, const StripLight& light);
    void Clear();

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
    StripLightSystem() = default;

    bool           enabled_     = false;
    SDL_GPUDevice* dev_         = nullptr;
    GpuPipeline    pipeline_;
    GpuStaticBuffer box_verts_;   // unit AABB [-1,1]^3, 36 verts (12 tris)

    StripLight lights_[MAX_STRIP_LIGHTS] = {};
    bool       active_[MAX_STRIP_LIGHTS] = {};
    int        light_count_              = 0;
};

} // namespace md
#endif // MD_SDL_GPU
