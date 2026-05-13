#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

// ── SMAASystem ─────────────────────────────────────────────────────────────────
// 3-pass post-process anti-aliasing (Subpixel Morphological AA structure).
// Pass 1: edge detect  (luma-based)     → RT_edges  RGBA8
// Pass 2: blend weight (neighbor gather) → RT_blend  RGBA8
// Pass 3: final blend  (color mix)       → swapchain
//
// Gated by RenderTierSystem::IsDeferred() (requires deferred path).
// For HD 520 (Forward tier) returns immediately — AA skipped.
//
// Usage:
//   Init(dev, w, h)
//   Render(cmd, input_tex, swapchain_tex)  — 3-pass AA
//   Shutdown()

namespace md {

class SMAASystem {
public:
    static SMAASystem& Get();

    void Init(SDL_GPUDevice* dev, int w, int h);
    void Shutdown();

    // Run all 3 passes. input_tex is the pre-AA swapchain or HDR buffer.
    // swapchain_tex is the final output surface.
    void Render(SDL_GPUCommandBuffer* cmd,
                SDL_GPUTexture*       input_tex,
                SDL_GPUTexture*       swapchain_tex);

    bool IsEnabled() const { return enabled_; }

private:
    SMAASystem() = default;

    bool           enabled_    = false;
    SDL_GPUDevice* dev_        = nullptr;
    int            w_ = 0, h_ = 0;

    SDL_GPUTexture* rt_edges_   = nullptr;  // RGBA8, pass 1 output
    SDL_GPUTexture* rt_blend_   = nullptr;  // RGBA8, pass 2 output
    SDL_GPUSampler* linear_sampler_ = nullptr;

    GpuPipeline edge_pipe_;   // pass 1: luma edge detect
    GpuPipeline blend_pipe_;  // pass 2: blend weight gather
    GpuPipeline final_pipe_;  // pass 3: color blend to swapchain
};

} // namespace md
#endif // MD_SDL_GPU
