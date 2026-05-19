#ifdef MD_SDL_GPU
#include <monkey_dust/render/bloom_system.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

namespace md {

BloomSystem& BloomSystem::Get() {
    static BloomSystem inst;
    return inst;
}

// ── helpers ──────────────────────────────────────────────────────────────────

static SDL_GPUTexture* CreateRT(SDL_GPUDevice* dev, int w, int h,
                                SDL_GPUTextureFormat fmt) {
    SDL_GPUTextureCreateInfo ti = {};
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.width                = (Uint32)w;
    ti.height               = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = 1;
    ti.format               = fmt;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                            | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    return SDL_CreateGPUTexture(dev, &ti);
}

static SDL_GPUSampler* CreateLinearSampler(SDL_GPUDevice* dev) {
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_LINEAR;
    si.mag_filter     = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    return SDL_CreateGPUSampler(dev, &si);
}

// Build a fullscreen-triangle pipeline targeting a specific format (or swapchain).
static bool MakeFullscreenPipe(SDL_GPUTextureFormat fmt,
                                const char* frag,
                                int samplers, int ubos,
                                bool blend_additive,
                                GpuPipeline& out) {
    GpuPipeline::Desc d;
    d.vert_path           = "shaders/deferred_lighting.vert";
    d.frag_path           = frag;
    d.layout.count        = 0;
    d.layout.stride       = 0;
    d.raster.blend_enable = blend_additive;
    if (blend_additive) {
        d.raster.src_factor = GpuBlendFactor::ONE;
        d.raster.dst_factor = GpuBlendFactor::ONE;
    }
    d.raster.depth_test   = false;
    d.raster.depth_write  = false;
    d.raster.cull_back    = false;
    d.frag_samplers       = (uint32_t)samplers;
    d.frag_uniform_bufs   = (uint32_t)ubos;
    d.has_depth_target    = false;
    d.color_format        = fmt;
    return out.Create(d);
}

// ── Init ─────────────────────────────────────────────────────────────────────

void BloomSystem::Init(SDL_GPUDevice* dev, int w, int h) {
    if (!RenderTierSystem::Get().IsDeferred()) {
        MD_LOG(MD_LOG_INFO, "BloomSystem: Forward tier — disabled");
        return;
    }
    dev_ = dev;
    w_  = w;  h_  = h;
    qw_ = w / 4; qh_ = h / 4;

    // Scene color RT (RGBA16F full-res — scene renders here instead of swapchain)
    scene_color_ = CreateRT(dev, w, h, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    if (!scene_color_) {
        MD_LOG(MD_LOG_WARNING, "BloomSystem: scene_color create failed: %s", SDL_GetError());
        return;
    }

    // Quarter-res bloom textures (RGBA16F)
    bloom_src_  = CreateRT(dev, qw_, qh_, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    bloom_temp_ = CreateRT(dev, qw_, qh_, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    bloom_out_  = CreateRT(dev, qw_, qh_, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    if (!bloom_src_ || !bloom_temp_ || !bloom_out_) {
        MD_LOG(MD_LOG_WARNING, "BloomSystem: bloom RT create failed");
        return;
    }

    scene_sampler_ = CreateLinearSampler(dev);
    bloom_sampler_ = CreateLinearSampler(dev);
    if (!scene_sampler_ || !bloom_sampler_) {
        MD_LOG(MD_LOG_WARNING, "BloomSystem: sampler create failed");
        return;
    }

    // Pipelines (all use deferred_lighting.vert — fullscreen triangle)
    bool ok = true;
    ok &= MakeFullscreenPipe(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                              "shaders/bloom_downsample.frag", 1, 1, false,
                              downsample_pipe_);
    ok &= MakeFullscreenPipe(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                              "shaders/bloom_blur_h.frag", 1, 1, false,
                              blur_h_pipe_);
    ok &= MakeFullscreenPipe(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                              "shaders/bloom_blur_v.frag", 1, 1, false,
                              blur_v_pipe_);
    // Composite: swapchain format (INVALID=auto-detect), 2 samplers, no additive
    ok &= MakeFullscreenPipe(SDL_GPU_TEXTUREFORMAT_INVALID,
                              "shaders/bloom_composite.frag", 2, 1, false,
                              composite_pipe_);
    if (!ok) {
        MD_LOG(MD_LOG_WARNING, "BloomSystem: one or more pipelines failed");
        return;
    }

    enabled_ = true;
    MD_LOG(MD_LOG_INFO, "BloomSystem: %dx%d RGBA16F scene + %dx%d bloom ready",
           w_, h_, qw_, qh_);
}

// ── SceneColorTargetInfo ──────────────────────────────────────────────────────

SDL_GPUColorTargetInfo BloomSystem::SceneColorTargetInfo() const {
    SDL_GPUColorTargetInfo ct = {};
    ct.texture     = scene_color_;
    ct.load_op     = SDL_GPU_LOADOP_CLEAR;
    ct.store_op    = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0.f, 0.f, 0.f, 1.f };
    return ct;
}

// ── ComputePass ───────────────────────────────────────────────────────────────

// Internal: begin + draw + end a fullscreen pass on a quarter-res target
static void QtrPass(SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline* pipe,
                    SDL_GPUTexture* target, int tw, int th,
                    const SDL_GPUTextureSamplerBinding* sbs, int nsbs,
                    const void* ubo, uint32_t ubo_sz) {
    SDL_GPUColorTargetInfo ct = {};
    ct.texture  = target;
    ct.load_op  = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) return;

    SDL_BindGPUGraphicsPipeline(pass, pipe);
    if (nsbs > 0) SDL_BindGPUFragmentSamplers(pass, 0, sbs, (Uint32)nsbs);
    if (ubo)      SDL_PushGPUFragmentUniformData(cmd, 0, ubo, ubo_sz);

    SDL_GPUViewport vp = { 0.f, 0.f, (float)tw, (float)th, 0.f, 1.f };
    SDL_SetGPUViewport(pass, &vp);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void BloomSystem::ComputePass(SDL_GPUCommandBuffer* cmd) {
    if (!enabled_) return;

    // ── 1. Downsample + threshold: scene_color → bloom_src (quarter-res) ─────
    {
        BloomDownsampleUBO ubo = {
            1.f / (float)w_, 1.f / (float)h_,   // full-res pixel size
            bloom_threshold, 0.f
        };
        SDL_GPUTextureSamplerBinding sb = { scene_color_, scene_sampler_ };
        QtrPass(cmd, downsample_pipe_.SDLPipeline(),
                bloom_src_, qw_, qh_, &sb, 1, &ubo, sizeof(ubo));
    }

    // ── 2. Gaussian5 horizontal: bloom_src → bloom_temp ──────────────────────
    {
        BloomBlurUBO ubo = { 1.f/(float)qw_, 1.f/(float)qh_, {0.f,0.f} };
        SDL_GPUTextureSamplerBinding sb = { bloom_src_, bloom_sampler_ };
        QtrPass(cmd, blur_h_pipe_.SDLPipeline(),
                bloom_temp_, qw_, qh_, &sb, 1, &ubo, sizeof(ubo));
    }

    // ── 3. Gaussian5 vertical: bloom_temp → bloom_out ────────────────────────
    {
        BloomBlurUBO ubo = { 1.f/(float)qw_, 1.f/(float)qh_, {0.f,0.f} };
        SDL_GPUTextureSamplerBinding sb = { bloom_temp_, bloom_sampler_ };
        QtrPass(cmd, blur_v_pipe_.SDLPipeline(),
                bloom_out_, qw_, qh_, &sb, 1, &ubo, sizeof(ubo));
    }
}

// ── CompositePass ─────────────────────────────────────────────────────────────

void BloomSystem::CompositePass(SDL_GPUCommandBuffer* cmd,
                                 SDL_GPUTexture* swapchain_tex, int sw, int sh) {
    if (!enabled_ || !swapchain_tex) return;

    // Open CLEAR pass on swapchain
    SDL_GPUColorTargetInfo ct = {};
    ct.texture     = swapchain_tex;
    ct.load_op     = SDL_GPU_LOADOP_CLEAR;
    ct.store_op    = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0.f, 0.f, 0.f, 1.f };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) return;

    SDL_BindGPUGraphicsPipeline(pass, composite_pipe_.SDLPipeline());

    SDL_GPUTextureSamplerBinding sbs[2] = {
        { scene_color_, scene_sampler_ },
        { bloom_out_,   bloom_sampler_ }
    };
    SDL_BindGPUFragmentSamplers(pass, 0, sbs, 2);

    BloomCompositeUBO ubo = {};
    ubo.bloom_intensity = bloom_intensity;
    ubo.exposure        = exposure;
    ubo.gamma           = gamma;
    // Copy colour grade polynomial (4 × 4 floats)
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            ubo.eq[i][j] = grade_eq[i][j];
    SDL_PushGPUFragmentUniformData(cmd, 0, &ubo, sizeof(ubo));

    SDL_GPUViewport vp = { 0.f, 0.f, (float)sw, (float)sh, 0.f, 1.f };
    SDL_SetGPUViewport(pass, &vp);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    SDL_EndGPURenderPass(pass);
}

// ── SetBiomeGrade (VBfA-R5) ───────────────────────────────────────────────────
// Presets derived from VBfA PostProcessing TintEq + Kenshi biome palette.
// All use grade_eq[2].a=1.0 (linear identity) as base; other terms add tint.
void BloomSystem::SetBiomeGrade(const char* biome) {
    // Reset to neutral first
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            grade_eq[i][j] = 0.f;
    grade_eq[2][3] = 1.f;  // eq[2].a = 1 → linear identity
    gamma = 2.2f;

    if (!biome) return;

    if (!__builtin_strcmp(biome, "desert") || !__builtin_strcmp(biome, "dust")) {
        // Warm orange lift: boost reds, cut blues, neutral greens
        grade_eq[3][0] =  0.04f;   // +red constant (lift)
        grade_eq[3][1] =  0.01f;   // +green constant
        grade_eq[3][2] = -0.03f;   // -blue constant
        grade_eq[2][0] =  0.03f;   // +red linear
        grade_eq[2][2] = -0.02f;   // -blue linear
        gamma = 2.3f;              // slightly brighter
    } else if (!__builtin_strcmp(biome, "highlands") ||
               !__builtin_strcmp(biome, "grassland")) {
        // Lush green: boost green linear slightly
        grade_eq[2][1] =  0.03f;
        grade_eq[2][2] = -0.01f;
    } else if (!__builtin_strcmp(biome, "swamp") ||
               !__builtin_strcmp(biome, "wetlands")) {
        // Murky yellow-green: desaturate + warm mid
        grade_eq[2][0] =  0.02f;
        grade_eq[2][1] =  0.02f;
        grade_eq[2][2] = -0.04f;
        gamma = 2.35f;
    } else if (!__builtin_strcmp(biome, "volcanic") ||
               !__builtin_strcmp(biome, "badlands")) {
        // Dark orange-red: strong warm tint + darker midtones
        grade_eq[3][0] =  0.05f;
        grade_eq[3][2] = -0.04f;
        grade_eq[1][0] =  0.03f;   // quadratic red boost
        grade_eq[1][2] = -0.02f;
        gamma = 2.4f;
    } else if (!__builtin_strcmp(biome, "snow") ||
               !__builtin_strcmp(biome, "ice")) {
        // Cool blue: boost blues, reduce reds
        grade_eq[3][0] = -0.02f;
        grade_eq[3][2] =  0.04f;
        grade_eq[2][0] = -0.02f;
        grade_eq[2][2] =  0.04f;
        gamma = 2.1f;              // slightly dimmer gamma
    }
    // "scrubland", "canyon", "default", and unknown → neutral (already set)
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void BloomSystem::Shutdown() {
    if (!dev_) return;
    downsample_pipe_.Destroy();
    blur_h_pipe_.Destroy();
    blur_v_pipe_.Destroy();
    composite_pipe_.Destroy();
    auto rt = [&](SDL_GPUTexture*& t){ if(t){SDL_ReleaseGPUTexture(dev_,t);t=nullptr;} };
    auto sm = [&](SDL_GPUSampler*& s){ if(s){SDL_ReleaseGPUSampler(dev_,s);s=nullptr;} };
    rt(scene_color_); rt(bloom_src_); rt(bloom_temp_); rt(bloom_out_);
    sm(scene_sampler_); sm(bloom_sampler_);
    dev_ = nullptr; enabled_ = false;
}

} // namespace md
#endif // MD_SDL_GPU
