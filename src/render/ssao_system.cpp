#ifdef MD_SDL_GPU
#include <monkey_dust/render/ssao_system.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

// Legacy UBO for ssao.comp (16-tap compute path)
struct SSAOParams {
    float inv_vp[16];
    float full_w, full_h;
    float half_w, half_h;
    float radius_uv, bias, strength, power;
};
static_assert(sizeof(SSAOParams) == 96);

namespace md {

SSAOSystem& SSAOSystem::Get() {
    static SSAOSystem inst;
    return inst;
}

void SSAOSystem::Init(SDL_GPUDevice* dev, int full_w, int full_h,
                      float nz, float fz) {
    if (!RenderTierSystem::Get().HasSSAO()) {
        MD_LOG(MD_LOG_INFO, "SSAOSystem: tier < Deferred_Med — disabled");
        return;
    }
    dev_    = dev;
    full_w_ = full_w;
    full_h_ = full_h;
    half_w_ = full_w / 2;
    half_h_ = full_h / 2;
    near_z  = nz;
    far_z   = fz;

    // ── VBfA-R1: linear depth texture (R32F, half-res) ───────────────────────
    {
        SDL_GPUTextureCreateInfo ti = {};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.width                = (Uint32)half_w_;
        ti.height               = (Uint32)half_h_;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                                | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        linear_depth_ = SDL_CreateGPUTexture(dev, &ti);
        if (!linear_depth_) {
            MD_LOG(MD_LOG_WARNING, "SSAOSystem: linear_depth create failed: %s", SDL_GetError());
            return;
        }
    }

    // BILINEAR sampler — for reading linear_depth in AO main pass
    {
        SDL_GPUSamplerCreateInfo si = {};
        si.min_filter     = SDL_GPU_FILTER_LINEAR;
        si.mag_filter     = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        linear_sampler_ = SDL_CreateGPUSampler(dev, &si);
        if (!linear_sampler_) {
            MD_LOG(MD_LOG_WARNING, "SSAOSystem: linear_sampler create failed");
            return;
        }
    }

    // NEAREST sampler — for bilateral blur (reads packed edge flags)
    {
        SDL_GPUSamplerCreateInfo si = {};
        si.min_filter     = SDL_GPU_FILTER_NEAREST;
        si.mag_filter     = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        point_sampler_ = SDL_CreateGPUSampler(dev, &si);
        if (!point_sampler_) {
            MD_LOG(MD_LOG_WARNING, "SSAOSystem: point_sampler create failed");
            return;
        }
    }

    // ── VBfA-R1: prep pipeline (fullscreen triangle, depth→R32F) ─────────────
    {
        GpuPipeline::Desc d;
        d.vert_path           = "shaders/deferred_lighting.vert";  // same fullscreen tri
        d.frag_path           = "shaders/ssao_prep.frag";
        d.layout.count        = 0;
        d.layout.stride       = 0;
        d.raster.blend_enable = false;
        d.raster.depth_test   = false;
        d.raster.depth_write  = false;
        d.raster.cull_back    = false;
        d.vert_uniform_bufs   = 0;
        d.frag_samplers       = 1;   // set=0 binding=0: u_depth
        d.frag_uniform_bufs   = 1;   // set=1 binding=0: SSAOPrepUBO
        d.has_depth_target = false;
        d.depth_only       = false;
        d.color_format     = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;

        if (!prep_pipeline_.Create(d)) {
            MD_LOG(MD_LOG_WARNING, "SSAOSystem: prep pipeline create failed");
            return;
        }
    }

    // ── Legacy compute path (ssao.comp 16-tap) ────────────────────────────────
    {
        SDL_GPUTextureCreateInfo ti = {};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.width                = (Uint32)half_w_;
        ti.height               = (Uint32)half_h_;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE
                                | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ao_tex_ = SDL_CreateGPUTexture(dev, &ti);
    }
    if (ao_tex_) {
        SDL_GPUSamplerCreateInfo si = {};
        si.min_filter     = SDL_GPU_FILTER_LINEAR;
        si.mag_filter     = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ao_sampler_ = SDL_CreateGPUSampler(dev, &si);

        GpuComputePipeline::Desc cd;
        cd.glsl_path                      = "shaders/ssao.comp";
        cd.num_samplers                   = 2;
        cd.num_readwrite_storage_textures = 1;
        cd.num_uniform_buffers            = 1;
        cd.threadcount_x = 8;
        cd.threadcount_y = 8;
        cd.threadcount_z = 1;
        legacy_pipeline_.Create(cd);
    }

    enabled_ = true;
    MD_LOG(MD_LOG_INFO, "SSAOSystem: %dx%d R32F linear_depth ready (near=%.2f far=%.1f)",
           half_w_, half_h_, near_z, far_z);
}

void SSAOSystem::PrepPass(SDL_GPUCommandBuffer* cmd,
                          SDL_GPUTexture*       hw_depth,
                          SDL_GPUSampler*       hw_sampler) {
    if (!enabled_ || !linear_depth_ || !prep_pipeline_.SDLPipeline()) return;

    // Render to linear_depth_ at half-res (no depth attachment)
    SDL_GPUColorTargetInfo ct = {};
    ct.texture     = linear_depth_;
    ct.load_op     = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op    = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0, 0, 0, 0 };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "SSAOSystem::PrepPass: begin failed: %s", SDL_GetError());
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, prep_pipeline_.SDLPipeline());

    // set=0 binding=0: hw depth texture
    SDL_GPUTextureSamplerBinding sb = { hw_depth, hw_sampler };
    SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);

    // set=1 binding=0: SSAOPrepUBO
    SSAOPrepUBO ubo = { near_z, far_z, {0.f, 0.f} };
    SDL_PushGPUFragmentUniformData(cmd, 0, &ubo, sizeof(ubo));

    // Fullscreen triangle: 3 verts, no VBO
    SDL_GPUViewport vp = { 0.f, 0.f, (float)half_w_, (float)half_h_, 0.f, 1.f };
    SDL_SetGPUViewport(pass, &vp);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    SDL_EndGPURenderPass(pass);
}

void SSAOSystem::MainPass(SDL_GPUCommandBuffer*) {
    // VBfA-R2 — implemented in next sprint
}

void SSAOSystem::BlurPass(SDL_GPUCommandBuffer*) {
    // VBfA-R2 — implemented in next sprint
}

void SSAOSystem::Dispatch(SDL_GPUCommandBuffer* cmd,
                           SDL_GPUTexture*       gbuf_depth,
                           SDL_GPUTexture*       gbuf_rt1,
                           SDL_GPUSampler*       gbuf_sampler,
                           const float*          inv_view_proj_16,
                           float radius_uv, float bias,
                           float strength,  float power) {
    if (!enabled_) return;

    // Open compute pass — bind AO texture as readwrite (set=1, binding=0)
    SDL_GPUStorageTextureReadWriteBinding rw_tex = {};
    rw_tex.texture = ao_tex_;
    rw_tex.cycle   = false;

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1, nullptr, 0);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "SSAOSystem: SDL_BeginGPUComputePass failed: %s", SDL_GetError());
        return;
    }

    SDL_BindGPUComputePipeline(pass, legacy_pipeline_.SDLComputePipeline());

    // Bind depth+RT1 as samplers (set=0, binding=0..1)
    SDL_GPUTextureSamplerBinding samplers[2] = {
        { gbuf_depth, gbuf_sampler },
        { gbuf_rt1,   gbuf_sampler }
    };
    SDL_BindGPUComputeSamplers(pass, 0, samplers, 2);

    // Push uniform params (set=2, binding=0)
    SSAOParams params;
    memcpy(params.inv_vp, inv_view_proj_16, 64);
    params.full_w    = (float)full_w_;
    params.full_h    = (float)full_h_;
    params.half_w    = (float)half_w_;
    params.half_h    = (float)half_h_;
    params.radius_uv = radius_uv;
    params.bias      = bias;
    params.strength  = strength;
    params.power     = power;
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

    // Dispatch: one workgroup per 8×8 tile of the half-res AO texture
    uint32_t gx = ((uint32_t)half_w_ + 7) / 8;
    uint32_t gy = ((uint32_t)half_h_ + 7) / 8;
    SDL_DispatchGPUCompute(pass, gx, gy, 1);

    SDL_EndGPUComputePass(pass);
}

void SSAOSystem::Shutdown() {
    if (!dev_) return;
    prep_pipeline_.Destroy();
    legacy_pipeline_.Destroy();
    auto rel_tex = [&](SDL_GPUTexture*& t){
        if (t) { SDL_ReleaseGPUTexture(dev_, t); t = nullptr; }
    };
    auto rel_sam = [&](SDL_GPUSampler*& s){
        if (s) { SDL_ReleaseGPUSampler(dev_, s); s = nullptr; }
    };
    rel_tex(linear_depth_);
    rel_tex(ssao_raw_);
    rel_tex(ssao_blurred_);
    rel_tex(ao_tex_);
    rel_sam(linear_sampler_);
    rel_sam(point_sampler_);
    rel_sam(ao_sampler_);
    dev_     = nullptr;
    enabled_ = false;
}

} // namespace md
#endif // MD_SDL_GPU
