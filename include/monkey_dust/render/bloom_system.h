#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

// ── BloomSystem (VBfA-R4) ─────────────────────────────────────────────────────
// HDR Bloom: 4×4 downsample with luma threshold + Gaussian5×5 separable blur.
//
// Architecture — requires an intermediate scene color RT (not swapchain):
//   1. Scene renders into scene_color_ (RGBA16F, full-res).
//   2. ComputePass(cmd): bloom_src_ ← downsample+threshold(scene_color_)
//                        bloom_temp_ ← Gaussian5H(bloom_src_)
//                        bloom_out_  ← Gaussian5V(bloom_temp_)
//   3. CompositePass(cmd, swapchain, w, h): scene_color_ + bloom_out_ → swapchain
//
// Scene render integration in main.cpp:
//   if (BloomSystem::Get().IsEnabled()) {
//       // Open render pass manually on SceneColorTex() instead of swapchain
//       SDL_BeginGPURenderPass(cmd, SceneColorTargetInfo(), ...)
//   } else {
//       GpuRenderPass::BeginColor(...)   // → swapchain, as before
//   }
//
// Gate: RenderTierSystem::IsDeferred() → Deferred_Low+.
// VBfA reference: filter_Sampled4x4 + PS_Gaussian5Horiz/Vert + frame_buffer_postprocess.

namespace md {

class BloomSystem {
public:
    static BloomSystem& Get();

    // Allocate textures + pipelines. No-op if !IsDeferred().
    void Init(SDL_GPUDevice* dev, int w, int h);
    void Shutdown();

    // Scene color RT — render the entire scene here (instead of swapchain).
    SDL_GPUTexture* SceneColorTex()     const { return scene_color_; }
    SDL_GPUSampler* SceneColorSampler() const { return scene_sampler_; }

    // Helper: fill a SDL_GPUColorTargetInfo for the scene color RT (CLEAR).
    SDL_GPUColorTargetInfo SceneColorTargetInfo() const;

    // Downsample → H blur → V blur. Call after scene + SSAO on scene_color_.
    void ComputePass(SDL_GPUCommandBuffer* cmd);

    // Blit scene_color_ + bloom → swapchain_tex (open CLEAR pass internally).
    void CompositePass(SDL_GPUCommandBuffer* cmd,
                       SDL_GPUTexture* swapchain_tex, int sw, int sh);

    bool IsEnabled() const { return enabled_; }
    int  Width()  const { return w_; }
    int  Height() const { return h_; }

    float bloom_threshold = 0.9f;   // luma cut-off (VBfA PostProcessing_BloomScalarOffset)
    float bloom_intensity = 0.25f;  // additive scale
    float exposure        = 1.0f;   // scene exposure multiplier

private:
    BloomSystem() = default;

    SDL_GPUDevice*  dev_          = nullptr;
    bool            enabled_      = false;
    int w_ = 0, h_ = 0;    // full-res
    int qw_ = 0, qh_ = 0;  // quarter-res

    SDL_GPUTexture* scene_color_ = nullptr;  // RGBA16F full-res  — scene renders here
    SDL_GPUTexture* bloom_src_   = nullptr;  // RGBA16F qtr-res   — threshold downsample
    SDL_GPUTexture* bloom_temp_  = nullptr;  // RGBA16F qtr-res   — H blur intermediate
    SDL_GPUTexture* bloom_out_   = nullptr;  // RGBA16F qtr-res   — final V blur

    SDL_GPUSampler* scene_sampler_ = nullptr;  // LINEAR CLAMP for scene reads
    SDL_GPUSampler* bloom_sampler_ = nullptr;  // LINEAR CLAMP for bloom reads

    GpuPipeline downsample_pipe_;  // shaders/bloom_downsample.frag
    GpuPipeline blur_h_pipe_;      // shaders/bloom_blur_h.frag
    GpuPipeline blur_v_pipe_;      // shaders/bloom_blur_v.frag
    GpuPipeline composite_pipe_;   // shaders/bloom_composite.frag → swapchain
};

} // namespace md

// ── Bloom UBOs (std140 — must match shader declarations) ──────────────────────
struct BloomDownsampleUBO {
    float pixel_w;
    float pixel_h;
    float threshold;
    float _pad;
};
static_assert(sizeof(BloomDownsampleUBO) == 16);

struct BloomBlurUBO {
    float pixel_w;
    float pixel_h;
    float _pad[2];
};
static_assert(sizeof(BloomBlurUBO) == 16);

struct BloomCompositeUBO {
    float bloom_intensity;
    float exposure;
    float _pad[2];
};
static_assert(sizeof(BloomCompositeUBO) == 16);

#endif // MD_SDL_GPU
