#pragma once
// OitPass — Weighted Order Independent Transparency (McGuire & Bavoil 2013).
//
// Renders transparent geometry in two sub-passes:
//   1. Accumulation: opaque depth as read-only, 2 MRT targets (accum + reveal).
//   2. Composite: full-screen blend of accum/reveal over the opaque scene.
//
// Usage per frame:
//   oit.BeginAccum(cmd, opaque_depth_tex);  // open accumulation render pass
//   // ... draw transparent geometry via SDL_GPURenderPass* oit.AccumPass() ...
//   oit.EndAccum();                          // close accumulation pass
//   oit.Composite(cmd, swapchain_tex, vp_w, vp_h); // blend over scene
//
// Transparent vertex format: float[3] pos + float[4] color = 28 bytes stride.
// Use oit.AccumPipeline() to bind the accumulation pipeline.

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

namespace md {

class OitPass {
public:
    static constexpr int MAX_TRANSPARENT_VERTS = 8192;  // ~1365 quads

    bool Init(int vp_w, int vp_h);
    void Shutdown();
    void Resize(int vp_w, int vp_h);
    bool IsReady() const { return ready_; }

    // Per-frame: open the accumulation pass.
    // opaque_depth: read-only depth from the opaque render pass (may be null).
    // Returns the raw SDL_GPURenderPass* to draw transparent geometry into.
    SDL_GPURenderPass* BeginAccum(SDL_GPUCommandBuffer* cmd,
                                   SDL_GPUTexture* opaque_depth = nullptr);

    // Close the accumulation pass.
    void EndAccum();

    // The accumulation pipeline — bind once per frame before drawing transparents.
    SDL_GPUGraphicsPipeline* AccumPipeline() const { return accum_pipeline_; }

    // Composite the accumulated transparency over the current swapchain texture.
    // Renders a full-screen triangle with SRC_ALPHA blending.
    void Composite(SDL_GPUCommandBuffer* cmd,
                   SDL_GPUTexture* output_tex,
                   int vp_w, int vp_h);

private:
    void CreateTextures(int w, int h);
    void DestroyTextures();
    void CreatePipelines();
    void DestroyPipelines();

    // Accumulation targets.
    SDL_GPUTexture* accum_tex_  = nullptr;   // RGBA16F, blend ONE/ONE
    SDL_GPUTexture* reveal_tex_ = nullptr;   // R8,      blend ZERO/ONE_MINUS_SRC_COLOR
    SDL_GPUSampler* sampler_    = nullptr;   // nearest, for composite read

    // Raw SDL_GPU pipeline for accumulation (single target, additive blend).
    SDL_GPUGraphicsPipeline* accum_pipeline_ = nullptr;

    // Composite pipeline (full-screen triangle, alpha blend over opaque scene).
    // Built as raw SDL_GPU pipeline to avoid GpuPipeline wrapper limitations.
    SDL_GPUGraphicsPipeline* composite_raw_  = nullptr;
    GpuPipeline composite_pipeline_;  // unused stub — kept for future use

    SDL_GPURenderPass* accum_pass_ = nullptr;  // active during BeginAccum..EndAccum
    bool use_blit_composite_ = false;           // fallback if composite pipeline fails

    int  tex_w_ = 0, tex_h_ = 0;
    bool ready_ = false;
};

} // namespace md
#endif // MD_SDL_GPU
