#ifdef MD_SDL_GPU
#include <monkey_dust/render/motion_blur.h>
#include <monkey_dust/platform/md_log.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstring>

namespace md {

MotionBlurSystem& MotionBlurSystem::Get() {
    static MotionBlurSystem inst;
    return inst;
}

// ── Init ─────────────────────────────────────────────────────────────────────

void MotionBlurSystem::Init(SDL_GPUDevice* dev, int full_w, int full_h,
                             float nz, float fz) {
    // Gate: check render_settings.json for "motion_blur": true
    {
        uint32_t sz = 0;
        char* buf = md::fs_read_alloc("data/render_settings.json", &sz);
        bool want = false;
        if (buf) {
            const char* p = strstr(buf, "\"motion_blur\"");
            if (p) {
                p = strchr(p + 13, ':');
                if (p) {
                    while (*p == ':' || *p == ' ' || *p == '\t') ++p;
                    want = (strncmp(p, "true", 4) == 0);
                }
            }
            md::fs_free(buf);
        }
        if (!want) {
            MD_LOG(MD_LOG_INFO, "MotionBlurSystem: disabled (render_settings.json motion_blur=false)");
            return;
        }
    }

    dev_    = dev;
    full_w_ = full_w;
    full_h_ = full_h;
    half_w_ = full_w / 2;
    half_h_ = full_h / 2;
    near_z  = nz;
    far_z   = fz;

    // ── RGBA8 motion render target (half-res) ─────────────────────────────────
    {
        SDL_GPUTextureCreateInfo ti = {};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.width                = (Uint32)half_w_;
        ti.height               = (Uint32)half_h_;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                                | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        motion_rt_ = SDL_CreateGPUTexture(dev, &ti);
        if (!motion_rt_) {
            MD_LOG(MD_LOG_WARNING, "MotionBlurSystem: motion_rt create failed: %s", SDL_GetError());
            return;
        }
    }

    // ── NEAREST sampler for motion texture read ───────────────────────────────
    {
        SDL_GPUSamplerCreateInfo si = {};
        si.min_filter     = SDL_GPU_FILTER_NEAREST;
        si.mag_filter     = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        point_samp_ = SDL_CreateGPUSampler(dev, &si);
        if (!point_samp_) {
            MD_LOG(MD_LOG_WARNING, "MotionBlurSystem: point_samp create failed");
            return;
        }
    }

    // ── LINEAR sampler for scene color read ──────────────────────────────────
    {
        SDL_GPUSamplerCreateInfo si = {};
        si.min_filter     = SDL_GPU_FILTER_LINEAR;
        si.mag_filter     = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        linear_samp_ = SDL_CreateGPUSampler(dev, &si);
        if (!linear_samp_) {
            MD_LOG(MD_LOG_WARNING, "MotionBlurSystem: linear_samp create failed");
            return;
        }
    }

    // ── RGBA16F scene color RT (used when bloom is not active) ────────────────
    {
        SDL_GPUTextureCreateInfo ti = {};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.width                = (Uint32)full_w_;
        ti.height               = (Uint32)full_h_;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                                | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        scene_color_ = SDL_CreateGPUTexture(dev, &ti);
        if (!scene_color_) {
            MD_LOG(MD_LOG_WARNING, "MotionBlurSystem: scene_color create failed: %s", SDL_GetError());
            return;
        }
    }

    // ── Prep pipeline: depth → RGBA8 motion (half-res) ───────────────────────
    {
        GpuPipeline::Desc d;
        d.vert_path           = "shaders/deferred_lighting.vert";
        d.frag_path           = "shaders/motion_prep.frag";
        d.layout.count        = 0;
        d.layout.stride       = 0;
        d.raster.blend_enable = false;
        d.raster.depth_test   = false;
        d.raster.depth_write  = false;
        d.raster.cull_back    = false;
        d.frag_samplers       = 1;   // set=0 b=0: u_depth
        d.frag_uniform_bufs   = 1;   // set=1 b=0: MotionPrepUBO
        d.has_depth_target    = false;
        d.color_format        = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        if (!prep_pipeline_.Create(d)) {
            MD_LOG(MD_LOG_WARNING, "MotionBlurSystem: prep pipeline create failed");
            return;
        }
    }

    // ── Blur pipeline: motion + hdr_color → swapchain (full-res) ─────────────
    {
        GpuPipeline::Desc d;
        d.vert_path           = "shaders/deferred_lighting.vert";
        d.frag_path           = "shaders/motion_blur.frag";
        d.layout.count        = 0;
        d.layout.stride       = 0;
        d.raster.blend_enable = false;
        d.raster.depth_test   = false;
        d.raster.depth_write  = false;
        d.raster.cull_back    = false;
        d.frag_samplers       = 2;   // set=0 b=0: u_motion, b=1: u_color
        d.frag_uniform_bufs   = 1;   // set=1 b=0: MotionBlurUBO
        d.has_depth_target    = false;
        d.color_format        = SDL_GPU_TEXTUREFORMAT_INVALID; // swapchain format
        if (!blur_pipeline_.Create(d)) {
            MD_LOG(MD_LOG_WARNING, "MotionBlurSystem: blur pipeline create failed");
            return;
        }
    }

    enabled_ = true;
    MD_LOG(MD_LOG_INFO, "MotionBlurSystem: %dx%d motion RT, 8-tap blur ready",
           half_w_, half_h_);
}

// ── PrepPass ─────────────────────────────────────────────────────────────────

void MotionBlurSystem::PrepPass(SDL_GPUCommandBuffer* cmd,
                                 SDL_GPUTexture*       hw_depth,
                                 SDL_GPUSampler*       hw_sampler,
                                 const float*          inv_vp16,
                                 const float*          prev_vp16) {
    if (!enabled_ || !motion_rt_ || !prep_pipeline_.SDLPipeline()) return;

    SDL_GPUColorTargetInfo ct = {};
    ct.texture     = motion_rt_;
    ct.load_op     = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op    = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0.5f, 0.5f, 0.5f, 1.f }; // neutral motion

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "MotionBlurSystem::PrepPass: begin failed: %s", SDL_GetError());
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, prep_pipeline_.SDLPipeline());

    SDL_GPUTextureSamplerBinding sb = { hw_depth, hw_sampler };
    SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);

    MotionPrepUBO ubo;
    memcpy(ubo.inv_view_proj, inv_vp16,  64);
    memcpy(ubo.prev_view_proj, prev_vp16, 64);
    ubo.near_z  = near_z;
    ubo.far_z   = far_z;
    ubo._pad[0] = 0.f;
    ubo._pad[1] = 0.f;
    SDL_PushGPUFragmentUniformData(cmd, 0, &ubo, sizeof(ubo));

    SDL_GPUViewport vp = { 0.f, 0.f, (float)half_w_, (float)half_h_, 0.f, 1.f };
    SDL_SetGPUViewport(pass, &vp);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    SDL_EndGPURenderPass(pass);
}

// ── ApplyPass ─────────────────────────────────────────────────────────────────

void MotionBlurSystem::ApplyPass(SDL_GPUCommandBuffer* cmd,
                                  SDL_GPUTexture*       hdr_color_tex,
                                  SDL_GPUSampler*       linear_sampler,
                                  SDL_GPUTexture*       swapchain_tex,
                                  int sw, int sh) {
    if (!enabled_ || !motion_rt_ || !blur_pipeline_.SDLPipeline()) return;
    if (!hdr_color_tex || !swapchain_tex) return;

    // Write directly to swapchain (DONT_CARE = we replace the entire image)
    SDL_GPUColorTargetInfo ct = {};
    ct.texture  = swapchain_tex;
    ct.load_op  = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "MotionBlurSystem::ApplyPass: begin failed: %s", SDL_GetError());
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, blur_pipeline_.SDLPipeline());

    SDL_GPUSampler* col_samp = linear_sampler ? linear_sampler : linear_samp_;
    SDL_GPUTextureSamplerBinding sbs[2] = {
        { motion_rt_,    point_samp_ },  // set=0 b=0: u_motion
        { hdr_color_tex, col_samp   },  // set=0 b=1: u_color
    };
    SDL_BindGPUFragmentSamplers(pass, 0, sbs, 2);

    MotionBlurUBO ubo = { movement_scale_x, movement_scale_y, {0.f, 0.f} };
    SDL_PushGPUFragmentUniformData(cmd, 0, &ubo, sizeof(ubo));

    SDL_GPUViewport vp = { 0.f, 0.f, (float)sw, (float)sh, 0.f, 1.f };
    SDL_SetGPUViewport(pass, &vp);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    SDL_EndGPURenderPass(pass);
}

// ── Shutdown ─────────────────────────────────────────────────────────────────

void MotionBlurSystem::Shutdown() {
    if (!dev_) return;
    prep_pipeline_.Destroy();
    blur_pipeline_.Destroy();
    if (motion_rt_)   { SDL_ReleaseGPUTexture(dev_, motion_rt_);   motion_rt_   = nullptr; }
    if (scene_color_) { SDL_ReleaseGPUTexture(dev_, scene_color_); scene_color_ = nullptr; }
    if (point_samp_)  { SDL_ReleaseGPUSampler(dev_, point_samp_);  point_samp_  = nullptr; }
    if (linear_samp_) { SDL_ReleaseGPUSampler(dev_, linear_samp_); linear_samp_ = nullptr; }
    dev_     = nullptr;
    enabled_ = false;
}

} // namespace md
#endif // MD_SDL_GPU
