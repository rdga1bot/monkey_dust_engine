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

    // Composite accumulated transparency over the opaque scene.
    // scene_tex: read-only opaque scene (e.g. CasPass::SceneTex()).
    // output_tex: write target — MUST differ from scene_tex (Vulkan disallows same-tex read+write).
    // Uses manual blend in shader (blend_enable=false) to avoid Intel HD 520 driver crash.
    void Composite(SDL_GPUCommandBuffer* cmd,
                   SDL_GPUTexture* scene_tex,
                   SDL_GPUTexture* output_tex,
                   int vp_w, int vp_h);

private:
    void CreateTextures(int w, int h);
    void DestroyTextures();
    void CreatePipelines();
    void DestroyPipelines();

    // Accumulation targets.
    SDL_GPUTexture* accum_tex_   = nullptr;  // RGBA8, additive blend
    SDL_GPUTexture* merged_tex_  = nullptr;  // RGBA8, manual composite output (R8G8B8A8_UNORM)
    SDL_GPUTexture* reveal_tex_  = nullptr;  // unused — future 2-MRT upgrade
    SDL_GPUSampler* sampler_     = nullptr;  // nearest, for composite reads

    // Raw SDL_GPU pipeline for accumulation (single target, additive blend).
    SDL_GPUGraphicsPipeline* accum_pipeline_ = nullptr;

    // Manual-blend composite via COMPUTE — avoids all graphics pipeline paths that
    // crash Intel HD 520 / mesa anv (2-sampler graphics pipeline segfaults driver).
    // oit_composite_manual.comp: samplers(accum+scene) → imageStore(merged_tex_).
    GpuComputePipeline composite_compute_;

    // Legacy SDL pipeline fields (disabled — kept for documentation).
    GpuPipeline composite_pipeline_;            // graphics path — crashes with 2 samplers
    SDL_GPUGraphicsPipeline* composite_raw_ = nullptr;  // raw alpha-blend — crashes anv

    SDL_GPURenderPass* accum_pass_ = nullptr;  // active during BeginAccum..EndAccum
    bool use_blit_composite_ = false;           // fallback if composite pipeline fails

    int  tex_w_ = 0, tex_h_ = 0;
    bool ready_ = false;
};

} // namespace md
#endif // MD_SDL_GPU
