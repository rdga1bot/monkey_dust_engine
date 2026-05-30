#pragma once
// EvsmShadow — Exponential Variance Shadow Maps for soft shadows.
//
// Replaces CSM depth textures with RG32F moment textures.
// Cascade structure is preserved — only the storage/sampling format changes:
//   CSM:  3 × depth_only pass → GpuDepthTexture → PCF depth compare
//   EVSM: 3 × moment pass    → RG32F texture   → Chebyshev inequality
//
// Advantages over PCF:
//   - Single texture sample per shadow query (no multi-sample loop)
//   - ~2x faster than 3×3 PCF on Intel HD 520
//   - Naturally soft edges proportional to local depth variance
//   - Hardware bilinear filtering on moment maps is safe (unlike depth)
//
// Integration — replaces ShadowSystem depth passes completely:
//   1. Init(NUM_CASCADES)
//   2. for k in 0..NUM_CASCADES:
//        BeginMomentPass(cmd, k) → draw casters → EndMomentPass()
//   3. BindArrayForSampling(pass, slot) — binds 2D array sampler
//   Shader: include "evsm_sample.glsl", call SampleEVSMCascade()
//
// Parameters:
//   num_cascades: matches ShadowSystem::NUM_CASCADES (default 3)
//   map_size:     resolution per cascade (default 1024)
//   warp_c:       exponential warp factor (default 40.0)

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

namespace md {

class EvsmShadow {
public:
    static EvsmShadow& Get() { static EvsmShadow inst; return inst; }

    static constexpr int   NUM_CASCADES    = 3;
    static constexpr int   DEFAULT_MAP_SIZE = 1024;
    static constexpr float DEFAULT_WARP_C   = 40.f;

    // num_cascades must match ShadowSystem::NUM_CASCADES.
    bool Init(int num_cascades = NUM_CASCADES,
              int map_size     = DEFAULT_MAP_SIZE,
              float warp_c     = DEFAULT_WARP_C);
    void Shutdown();
    bool IsReady() const { return ready_; }

    // ── Shadow pass (one call per cascade) ───────────────────────────────────

    // Open moment-write pass for cascade k. Draw shadow casters, then EndMomentPass().
    // Vertex shader: shadow_csm.vert (same light-space transform as depth pass).
    SDL_GPURenderPass* BeginMomentPass(SDL_GPUCommandBuffer* cmd, int cascade);
    void               EndMomentPass();

    SDL_GPUGraphicsPipeline* MomentPipeline() const { return moment_pipeline_; }

    // ── VBfA R-3: Gaussian blur on moment maps ────────────────────────────────
    // 2-pass separable 5×5 Gaussian (shadow_blur_h/v.comp) run after all moment passes.
    // Ping-pong: moment_tex_[k] → blur_tmp_[k] → moment_tex_[k].
    // Call once per frame AFTER all EndMomentPass() calls, before sampling.
    void ApplyBlur(SDL_GPUCommandBuffer* cmd);
    bool BlurReady() const { return blur_ready_; }

    // ── Sampling ──────────────────────────────────────────────────────────────

    // Bind the 2D array moment texture (all cascades) as a fragment sampler.
    // Shader uses SampleEVSMCascade(evsm_array, cascade_idx, uv, depth, warp_c).
    void BindArrayForSampling(SDL_GPURenderPass* pass, uint32_t slot);

    // Per-cascade texture (for direct access or debug blit).
    SDL_GPUTexture* MomentTex(int k) const {
        return (k >= 0 && k < num_cascades_) ? moment_tex_[k] : nullptr;
    }
    SDL_GPUSampler* MomentSampler() const { return sampler_; }

    int   NumCascades() const { return num_cascades_; }
    int   MapSize()     const { return map_size_; }
    float WarpC()       const { return warp_c_; }

private:
    SDL_GPUTexture*          moment_tex_[NUM_CASCADES] = {};  // RG32F (or RG16F fallback) per cascade
    SDL_GPUTextureFormat     moment_fmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;  // actual format used
    SDL_GPUSampler*          sampler_         = nullptr;      // bilinear, clamp
    SDL_GPURenderPass*       moment_pass_     = nullptr;
    SDL_GPUGraphicsPipeline* moment_pipeline_ = nullptr;

    // ── VBfA R-3: blur resources ──────────────────────────────────────────────
    // blur_tmp_: H-pass output (moment_tex → blur_tmp)
    // blur_out_: V-pass output (blur_tmp → blur_out); sampled instead of moment_tex_.
    // Neither moment_tex_ nor its format needs COMPUTE_STORAGE_WRITE.
    SDL_GPUTexture*   blur_tmp_[NUM_CASCADES] = {};  // H-pass ping-pong intermediate
    SDL_GPUTexture*   blur_out_[NUM_CASCADES] = {};  // V-pass final output (sampled)
    GpuComputePipeline blur_h_cs_;  // shadow_blur_h.comp
    GpuComputePipeline blur_v_cs_;  // shadow_blur_v.comp
    bool blur_ready_ = false;

    int   num_cascades_ = NUM_CASCADES;
    int   map_size_     = DEFAULT_MAP_SIZE;
    float warp_c_       = DEFAULT_WARP_C;
    bool  ready_        = false;
};

} // namespace md
#endif // MD_SDL_GPU
