#ifdef MD_SDL_GPU
#include <monkey_dust/render/ssao_system.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

// std140-aligned UBO; must match ssao.comp SSAOParams
struct SSAOParams {
    float inv_vp[16];           // mat4 @  0
    float full_w, full_h;       // vec4 @ 64 (screen_size xy)
    float half_w, half_h;       //       64 (screen_size zw)
    float radius_uv;            // vec4 @ 80 (ao_params x)
    float bias;                 //       80 (ao_params y)
    float strength;             //       80 (ao_params z)
    float power;                //       80 (ao_params w)
};
static_assert(sizeof(SSAOParams) == 96);

namespace md {

SSAOSystem& SSAOSystem::Get() {
    static SSAOSystem inst;
    return inst;
}

void SSAOSystem::Init(SDL_GPUDevice* dev, int full_w, int full_h) {
    if (!RenderTierSystem::Get().HasSSAO()) {
        MD_LOG(MD_LOG_INFO, "SSAOSystem: tier < Deferred_Med — disabled");
        return;
    }
    dev_   = dev;
    full_w_ = full_w;
    full_h_ = full_h;
    half_w_ = full_w / 2;
    half_h_ = full_h / 2;

    // AO output: half-res R8_UNORM — write in compute, sample in lighting pass
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
    if (!ao_tex_) {
        MD_LOG(MD_LOG_WARNING, "SSAOSystem: AO texture create failed: %s", SDL_GetError());
        return;
    }

    // LINEAR + CLAMP sampler for upsampled AO reads in the lighting pass
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_LINEAR;
    si.mag_filter     = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ao_sampler_ = SDL_CreateGPUSampler(dev, &si);
    if (!ao_sampler_) {
        MD_LOG(MD_LOG_WARNING, "SSAOSystem: AO sampler create failed");
        return;
    }

    GpuComputePipeline::Desc d;
    d.glsl_path                     = "shaders/ssao.comp";
    d.num_samplers                  = 2;  // uDepth + uRT1 at set=0, binding=0..1
    d.num_readonly_storage_textures = 0;
    d.num_readonly_storage_buffers  = 0;
    d.num_readwrite_storage_textures = 1; // uAO at set=1, binding=0
    d.num_readwrite_storage_buffers  = 0;
    d.num_uniform_buffers           = 1;  // SSAOParams at set=2, binding=0
    d.threadcount_x = 8;
    d.threadcount_y = 8;
    d.threadcount_z = 1;

    if (!pipeline_.Create(d)) {
        MD_LOG(MD_LOG_WARNING, "SSAOSystem: compute pipeline create failed");
        return;
    }

    enabled_ = true;
    MD_LOG(MD_LOG_INFO, "SSAOSystem: %dx%d (half of %dx%d) — R8_UNORM AO",
           half_w_, half_h_, full_w_, full_h_);
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

    SDL_BindGPUComputePipeline(pass, pipeline_.SDLComputePipeline());

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
    pipeline_.Destroy();
    if (ao_tex_)     { SDL_ReleaseGPUTexture(dev_, ao_tex_);   ao_tex_     = nullptr; }
    if (ao_sampler_) { SDL_ReleaseGPUSampler(dev_, ao_sampler_); ao_sampler_ = nullptr; }
    dev_     = nullptr;
    enabled_ = false;
}

} // namespace md
#endif // MD_SDL_GPU
