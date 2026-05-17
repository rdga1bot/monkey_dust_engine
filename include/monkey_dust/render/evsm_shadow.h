#pragma once
// EvsmShadow — Exponential Variance Shadow Maps for soft shadows.
//
// Drop-in upgrade over standard PCF CSM:
//   PCF:  samples depth texture + comparison → binary result
//   EVSM: samples RG32F moment texture → probabilistic soft edge
//
// Advantages over PCSS (Percentage Closer Soft Shadows):
//   - Single texture sample per shadow query (no multi-sample loop)
//   - ~2x faster than 3×3 PCF, much faster than PCSS
//   - Naturally soft edges proportional to local depth variance
//
// Integration with existing ShadowSystem:
//   1. Init()         — creates moment texture + moment-write pipeline
//   2. BeginPass(cmd) — opens colour render pass targeting moment_tex_
//   3. Draw shadow casters (same vertex shader as CSM depth pass)
//   4. EndPass()
//   5. BindForSampling(pass, slot) — binds moment_tex_ as sampler
//
// Shader side: include "evsm_sample.glsl" and call SampleEVSM().
//
// Parameters:
//   map_size: resolution of the moment map (default 1024, matches CSM)
//   warp_c:   exponential warp factor (default 40.0 — good for world scale)

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

namespace md {

class EvsmShadow {
public:
    static EvsmShadow& Get() { static EvsmShadow inst; return inst; }

    static constexpr int   DEFAULT_MAP_SIZE = 1024;
    static constexpr float DEFAULT_WARP_C   = 40.f;

    bool Init(int map_size = DEFAULT_MAP_SIZE, float warp_c = DEFAULT_WARP_C);
    void Shutdown();
    bool IsReady() const { return ready_; }

    // ── Shadow pass ───────────────────────────────────────────────────────────

    // Open the moment-write render pass. Draw shadow casters between Begin/End.
    // The vertex shader is the same as the standard CSM depth pass.
    // depth_tex: optional read-only depth for early-Z culling (may be null).
    SDL_GPURenderPass* BeginMomentPass(SDL_GPUCommandBuffer* cmd,
                                        SDL_GPUTexture* depth_tex = nullptr);
    void EndMomentPass();

    // The graphics pipeline for writing moments.
    // Bind with SDL_BindGPUGraphicsPipeline before drawing shadow casters.
    SDL_GPUGraphicsPipeline* MomentPipeline() const { return moment_pipeline_; }

    // ── Sampling ──────────────────────────────────────────────────────────────

    // Bind the moment texture as a fragment sampler for the main render pass.
    // slot: the sampler binding index used by your main shader.
    void BindForSampling(SDL_GPURenderPass* pass, uint32_t slot);

    // Raw moment texture — for manual binding or blit.
    SDL_GPUTexture* MomentTex()  const { return moment_tex_; }
    SDL_GPUSampler* MomentSampler() const { return sampler_; }

    int   MapSize() const { return map_size_; }
    float WarpC()   const { return warp_c_; }

private:
    SDL_GPUTexture*           moment_tex_      = nullptr;  // RG32F moments
    SDL_GPUSampler*           sampler_         = nullptr;  // bilinear, clamp
    SDL_GPURenderPass*        moment_pass_     = nullptr;
    SDL_GPUGraphicsPipeline*  moment_pipeline_ = nullptr;

    int   map_size_ = DEFAULT_MAP_SIZE;
    float warp_c_   = DEFAULT_WARP_C;
    bool  ready_    = false;
};

} // namespace md
#endif // MD_SDL_GPU
