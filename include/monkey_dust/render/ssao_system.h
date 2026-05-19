#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

// ── SSAOSystem (VBfA-R1 + R2) ─────────────────────────────────────────────────
// Screen-Space Ambient Occlusion — VBfA-inspired approach.
//
// VBfA-R1 PrepPass: fragment-shader depth linearization.
//   Reads HW depth (D24) → writes R32F linear view-space depth at half-res.
//   No GBuffer RT1 needed — normals reconstructed from depth gradient in R2.
//
// VBfA-R2 MainPass (upcoming): 8-sample Poisson AO + bilateral blur.
//   Reads linear_depth → writes ssao_raw RGBA8 (R=AO, GB=packed edge flags).
//
// Legacy compute path (ssao.comp 16-tap) preserved as Dispatch() for reference.
//
// Frame order:
//   1. Geometry → depth buffer filled
//   2. SSAOSystem::PrepPass(cmd, hw_depth, hw_sampler)  ← VBfA-R1
//   3. SSAOSystem::MainPass(cmd)                        ← VBfA-R2 (upcoming)
//   4. SSAOSystem::BlurPass(cmd)                        ← VBfA-R2
//   5. Deferred lighting reads linear_depth / ssao_blurred
//
// Gate: IsEnabled() — true when RenderTierSystem::HasSSAO().

namespace md {

class SSAOSystem {
public:
    static SSAOSystem& Get();

    // Allocate textures + pipelines.  No-op if !HasSSAO().
    // near_z / far_z: camera planes for depth linearization.
    void Init(SDL_GPUDevice* dev, int full_w, int full_h,
              float near_z = 0.1f, float far_z = 500.0f);
    void Shutdown();

    // VBfA-R1: linearize HW depth → linear_depth R32F (half-res).
    // hw_depth:   the scene depth texture after geometry pass (D24_UNORM_S8_UINT).
    // hw_sampler: NEAREST+CLAMP sampler compatible with hw_depth.
    // cmd must NOT be inside an active render pass.
    void PrepPass(SDL_GPUCommandBuffer* cmd,
                  SDL_GPUTexture*       hw_depth,
                  SDL_GPUSampler*       hw_sampler);

    // VBfA-R2 stubs (implemented in R2 sprint).
    void MainPass(SDL_GPUCommandBuffer* cmd);  // linear_depth → ssao_raw
    void BlurPass(SDL_GPUCommandBuffer* cmd);  // bilateral blur → ssao_blurred

    // Legacy 16-tap compute path (ssao.comp) — kept for reference/testing.
    void Dispatch(SDL_GPUCommandBuffer* cmd,
                  SDL_GPUTexture*       gbuf_depth,
                  SDL_GPUTexture*       gbuf_rt1,
                  SDL_GPUSampler*       gbuf_sampler,
                  const float*          inv_view_proj_16,
                  float radius_uv  = 0.05f,
                  float bias       = 0.002f,
                  float strength   = 0.8f,
                  float power      = 1.5f);

    // Textures
    SDL_GPUTexture* LinearDepthTex()  const { return linear_depth_; }
    SDL_GPUTexture* SSAORawTex()      const { return ssao_raw_;     }
    SDL_GPUTexture* SSAOBlurredTex()  const { return ssao_blurred_; }
    SDL_GPUTexture* AOTexture()       const { return ao_tex_;        } // legacy

    // Samplers
    SDL_GPUSampler* LinearSampler()   const { return linear_sampler_; }
    SDL_GPUSampler* PointSampler()    const { return point_sampler_;  }
    SDL_GPUSampler* AOSampler()       const { return ao_sampler_;     } // legacy

    bool IsEnabled() const { return enabled_; }
    int  HalfW()     const { return half_w_; }
    int  HalfH()     const { return half_h_; }

    float near_z = 0.1f;
    float far_z  = 500.0f;

private:
    SSAOSystem() = default;

    bool              enabled_        = false;
    SDL_GPUDevice*    dev_            = nullptr;

    // VBfA-R1: linear depth prep
    GpuPipeline       prep_pipeline_;            // fragment-shader depth linearize
    SDL_GPUTexture*   linear_depth_  = nullptr;  // R32F half-res
    SDL_GPUSampler*   linear_sampler_= nullptr;  // BILINEAR for final reads
    SDL_GPUSampler*   point_sampler_ = nullptr;  // NEAREST for bilateral blur

    // VBfA-R2: AO output (stubs — nullptr until R2)
    SDL_GPUTexture*   ssao_raw_      = nullptr;  // RGBA8 (R=AO, GB=packed edges)
    SDL_GPUTexture*   ssao_blurred_  = nullptr;  // R8 final blurred AO

    // Legacy compute AO
    GpuComputePipeline legacy_pipeline_;
    SDL_GPUTexture*   ao_tex_        = nullptr;  // R8_UNORM legacy
    SDL_GPUSampler*   ao_sampler_    = nullptr;  // LINEAR legacy

    int half_w_ = 0, half_h_ = 0;
    int full_w_ = 0, full_h_ = 0;
};

} // namespace md

// ── SSAOPrepUBO (std140, 16 bytes — must match ssao_prep.frag) ─────────────────
struct SSAOPrepUBO {
    float near_z;
    float far_z;
    float _pad[2];
};
static_assert(sizeof(SSAOPrepUBO) == 16);

#endif // MD_SDL_GPU
