#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

// ── SSAOSystem ─────────────────────────────────────────────────────────────────
// Screen-space ambient occlusion — half-resolution compute (640×360 @ 1280×720).
// 16-tap rotated disk sampler; output R8_UNORM texture (SSBO binding=9 analog).
// Gate: RenderTierSystem::Get().HasSSAO() → Deferred_Med+.
//
// SPIR-V resource layout (SDL3 set convention):
//   set=0 binding=0: uDepth sampler  (scene depth, sampled)
//   set=0 binding=1: uRT1   sampler  (oct-normal+metallic, sampled)
//   set=1 binding=0: uAO    image2D  (R8_UNORM, writeonly, COMPUTE_STORAGE_WRITE)
//   set=2 binding=0: SSAOParams UBO
//
// Usage:
//   Init(dev, full_w, full_h)           — once after GpuDevice ready
//   Dispatch(cmd, depth, rt1, sampler, inv_vp, radius, bias, strength)
//   AOTexture()/AOSampler()             — bind in lighting/composite passes
//   Shutdown()

namespace md {

class SSAOSystem {
public:
    static SSAOSystem& Get();

    void Init(SDL_GPUDevice* dev, int full_w, int full_h);
    void Shutdown();

    // Dispatch half-res AO compute. Call after GBuffer::End(), before lighting.
    // gbuf_sampler: NEAREST+CLAMP from GBuffer (reused for depth+RT1).
    // inv_view_proj_16: inverse of VP matrix used in GBuffer pass.
    // radius_ws: AO sample radius in UV space (e.g. 0.05)
    // bias: depth comparison bias (e.g. 0.002)
    // strength: occlusion multiplier [0..1]
    // power: output curve exponent (e.g. 1.5)
    void Dispatch(SDL_GPUCommandBuffer* cmd,
                  SDL_GPUTexture*       gbuf_depth,
                  SDL_GPUTexture*       gbuf_rt1,
                  SDL_GPUSampler*       gbuf_sampler,
                  const float*          inv_view_proj_16,
                  float radius_uv  = 0.05f,
                  float bias       = 0.002f,
                  float strength   = 0.8f,
                  float power      = 1.5f);

    SDL_GPUTexture* AOTexture() const { return ao_tex_; }
    SDL_GPUSampler* AOSampler() const { return ao_sampler_; }
    bool IsEnabled() const { return enabled_; }
    int  HalfW()    const { return half_w_; }
    int  HalfH()    const { return half_h_; }

private:
    SSAOSystem() = default;

    bool              enabled_    = false;
    SDL_GPUDevice*    dev_        = nullptr;
    GpuComputePipeline pipeline_;
    SDL_GPUTexture*   ao_tex_     = nullptr;
    SDL_GPUSampler*   ao_sampler_ = nullptr;
    int half_w_ = 0, half_h_ = 0;
    int full_w_ = 0, full_h_ = 0;
};

} // namespace md
#endif // MD_SDL_GPU
