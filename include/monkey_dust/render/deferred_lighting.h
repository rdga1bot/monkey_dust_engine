#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gbuffer.h>
#include <monkey_dust/render/gpu_hal.h>

// ── DeferredLightingSystem (M57) ──────────────────────────────────────────────
// Ambient + directional lighting pass for the deferred pipeline.
// Draws a fullscreen triangle (no VBO) reading GBuffer RT0/RT1.
// Output: HDR color texture (RGBA16F, same dimensions as GBuffer).
//
// Frame draw order:
//   1. GBuffer::Begin() + geometry draw + GBuffer::End()
//   2. SSAOSystem::Dispatch()   [gate: Deferred_Med+]
//   3. DeferredLightingSystem::DrawAmbientPass(cmd, gbuf)   ← this
//   4. PointLightSystem::Draw()  — additive point volumes
//   5. StripLightSystem::Draw()  — additive strip capsules
//   6. Forward pass (alpha/particles)
//   7. SMAA (3-pass)
//
// Gate: call IsReady() before use.  Init() creates nothing if !GBufferSystem::IsEnabled().
// ShaderPaths: shaders/deferred_lighting.vert.spv + deferred_lighting.frag.spv

namespace md {

class DeferredLightingSystem {
public:
    static DeferredLightingSystem& Get();

    // Creates hdr_color texture + pipeline.
    // Requires GBufferSystem::Get().IsEnabled() == true; no-op otherwise.
    void Init(SDL_GPUDevice* dev, int w, int h);
    void Shutdown();

    // Draws ambient + directional pass from gbuf into HdrColorTex().
    // depth_tex: scene depth (D32_FLOAT) for EVSM world-pos reconstruction.
    //   Pass nullptr to skip shadow sampling (shadows render unshadowed).
    // cmd must not be inside an active render pass.
    void DrawAmbientPass(SDL_GPUCommandBuffer* cmd, const GBuffer& gbuf,
                         SDL_GPUTexture* depth_tex = nullptr);

    SDL_GPUTexture* HdrColorTex() const { return hdr_color_.SDLTexture(); }
    bool IsReady() const { return hdr_color_.SDLTexture() != nullptr; }

    // ── Lighting parameters (set before DrawAmbientPass each frame) ────────────
    float sun_dir[3]       = {  0.577f, -0.577f,  0.577f };
    float sun_color[3]     = {  1.00f,   0.95f,   0.80f  };
    float ambient_color[3] = {  0.15f,   0.18f,   0.22f  };
    float emissive_scale   = 1.4875f;

    // Call once per frame with the inverse camera view-projection matrix.
    // Required for EVSM world-position reconstruction from depth buffer.
    void SetInvViewProj(const float m[16]) { memcpy(inv_view_proj_, m, 64); }

private:
    DeferredLightingSystem() = default;

    SDL_GPUDevice*  dev_           = nullptr;
    GpuColorTexture hdr_color_;
    GpuSampler      sampler_nearest_;  // for GBuffer + depth
    GpuSampler      sampler_linear_;   // for EVSM moment maps
    GpuPipeline     pipeline_;
    float           inv_view_proj_[16] = {};
    int w_ = 0, h_ = 0;
};

} // namespace md

#endif // MD_SDL_GPU
