#ifdef MD_SDL_GPU
#include <monkey_dust/render/gbuffer.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/platform/md_log.h>
// ── GBuffer ───────────────────────────────────────────────────────────────────

void GBuffer::Init(SDL_GPUDevice* dev, int w, int h) {
    dev_ = dev; w_ = w; h_ = h;

    SDL_GPUTextureCreateInfo ti = {};
    ti.type              = SDL_GPU_TEXTURETYPE_2D;
    ti.width             = static_cast<Uint32>(w);
    ti.height            = static_cast<Uint32>(h);
    ti.layer_count_or_depth = 1;
    ti.num_levels        = 1;

    // RT0: albedo(RGB) + roughness(A) — RGBA8 unorm
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage  = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    rt0_ = SDL_CreateGPUTexture(dev, &ti);

    // RT1: oct-normal(RG) + metallic(B) + flags(A) — RGBA8 unorm
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage  = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    rt1_ = SDL_CreateGPUTexture(dev, &ti);

    // D32_FLOAT_S8_UINT: Intel Gen9 cannot sample D24_UNORM_S8_UINT (support check was
    // only for DEPTH_STENCIL_TARGET, missing SAMPLER — always use D32 for sampler safety).
    ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    ti.usage  = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    depth_ = SDL_CreateGPUTexture(dev, &ti);

    // Shared sampler: NEAREST + CLAMP_TO_EDGE (GBuffer reads must be pixel-precise)
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_NEAREST;
    si.mag_filter     = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(dev, &si);

    if (!rt0_ || !rt1_ || !depth_ || !sampler_) {
        MD_LOG(MD_LOG_WARNING, "GBuffer: texture creation failed (%dx%d)", w, h);
        return;
    }
    MD_LOG(MD_LOG_INFO, "GBuffer: %dx%d RT0+RT1+Depth — 8 bytes/pixel = %.1f MB",
           w, h, static_cast<float>(w * h * 8) / (1024.f * 1024.f));
}

SDL_GPURenderPass* GBuffer::Begin(SDL_GPUCommandBuffer* cmd) {
    SDL_GPUColorTargetInfo col[2] = {};

    // RT0 — albedo + roughness
    col[0].texture      = rt0_;
    col[0].load_op      = SDL_GPU_LOADOP_CLEAR;
    col[0].store_op     = SDL_GPU_STOREOP_STORE;
    col[0].clear_color  = { 0.f, 0.f, 0.f, 1.f };  // clear roughness=1 (fully rough)

    // RT1 — oct-normal + metallic + flags
    col[1].texture      = rt1_;
    col[1].load_op      = SDL_GPU_LOADOP_CLEAR;
    col[1].store_op     = SDL_GPU_STOREOP_STORE;
    col[1].clear_color  = { 0.5f, 0.5f, 0.f, 0.f }; // oct-normal=(0.5,0.5)=up

    SDL_GPUDepthStencilTargetInfo ds = {};
    ds.texture           = depth_;
    ds.load_op           = SDL_GPU_LOADOP_CLEAR;
    ds.store_op          = SDL_GPU_STOREOP_STORE;
    ds.stencil_load_op   = SDL_GPU_LOADOP_DONT_CARE;
    ds.stencil_store_op  = SDL_GPU_STOREOP_DONT_CARE;
    ds.clear_depth       = 1.f;
    ds.cycle             = false;

    pass_ = SDL_BeginGPURenderPass(cmd, col, 2, &ds);
    return pass_;
}

void GBuffer::End() {
    if (pass_) {
        SDL_EndGPURenderPass(pass_);
        pass_ = nullptr;
    }
}

void GBuffer::Shutdown() {
    if (!dev_) return;
    if (pass_)    { SDL_EndGPURenderPass(pass_); pass_ = nullptr; }
    if (sampler_) { SDL_ReleaseGPUSampler(dev_, sampler_); sampler_ = nullptr; }
    if (rt0_)     { SDL_ReleaseGPUTexture(dev_, rt0_);     rt0_     = nullptr; }
    if (rt1_)     { SDL_ReleaseGPUTexture(dev_, rt1_);     rt1_     = nullptr; }
    if (depth_)   { SDL_ReleaseGPUTexture(dev_, depth_);   depth_   = nullptr; }
    dev_ = nullptr;
}

// ── GBufferSystem ─────────────────────────────────────────────────────────────

namespace md {

GBufferSystem& GBufferSystem::Get() {
    static GBufferSystem inst;
    return inst;
}

void GBufferSystem::Init(SDL_GPUDevice* dev, int w, int h) {
    enabled_ = RenderTierSystem::Get().IsDeferred();
    if (!enabled_) {
        MD_LOG(MD_LOG_INFO, "GBufferSystem: Forward tier — GBuffer disabled");
        return;
    }
    gbuf_.Init(dev, w, h);
}

void GBufferSystem::Shutdown() {
    if (enabled_) gbuf_.Shutdown();
    enabled_ = false;
}

} // namespace md

#endif // MD_SDL_GPU
