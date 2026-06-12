#ifdef MD_SDL_GPU
#include <monkey_dust/render/deferred_lighting.h>
#include <monkey_dust/render/evsm_shadow.h>
#include <monkey_dust/render/shadow_system.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

// ── DeferredAmbientUBO — must match std140 layout in deferred_lighting.frag ──
// std140 offsets:
//   0   sun_dir           vec4   16B
//  16   sun_color         vec4   16B
//  32   ambient_color     vec4   16B
//  48   emissive_scale    float   4B
//  52   shadow_dist_scalar float  4B
//  56   shadow_height_scalar float 4B
//  60   shadow_height_offset float 4B
//  64   inv_view_proj     mat4   64B  (aligned 16B ✓)
// 128   light_vp[0]       mat4   64B
// 192   light_vp[1]       mat4   64B
// 256   light_vp[2]       mat4   64B
// 320   cascade_splits    vec4   16B
// 336   evsm_warp_c       float   4B
// 340   _pad[3]           float  12B
// 352   total
struct alignas(16) DeferredAmbientUBO {
    float sun_dir[4];            //   0
    float sun_color[4];          //  16
    float ambient_color[4];      //  32
    float emissive_scale;        //  48
    float shadow_dist_scalar;    //  52
    float shadow_height_scalar;  //  56
    float shadow_height_offset;  //  60
    float inv_view_proj[16];     //  64
    float light_vp[3][16];       // 128
    float cascade_splits[4];     // 320
    float evsm_warp_c;           // 336
    float _pad[3];               // 340
};
static_assert(sizeof(DeferredAmbientUBO) == 352,
              "DeferredAmbientUBO size must match deferred_lighting.frag std140");

namespace md {

DeferredLightingSystem& DeferredLightingSystem::Get() {
    static DeferredLightingSystem inst;
    return inst;
}

void DeferredLightingSystem::Init(SDL_GPUDevice* dev, int w, int h) {
    if (!RenderTierSystem::Get().IsDeferred()) {
        MD_LOG(MD_LOG_INFO, "DeferredLightingSystem: Forward tier — disabled");
        return;
    }
    dev_ = dev; w_ = w; h_ = h;

    // HDR color target (RGBA16F — enough range before tonemapping)
    SDL_GPUTextureCreateInfo ti = {};
    ti.type                    = SDL_GPU_TEXTURETYPE_2D;
    ti.width                   = static_cast<Uint32>(w);
    ti.height                  = static_cast<Uint32>(h);
    ti.layer_count_or_depth    = 1;
    ti.num_levels              = 1;
    ti.format                  = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    ti.usage                   = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                 SDL_GPU_TEXTUREUSAGE_SAMPLER;
    hdr_color_ = SDL_CreateGPUTexture(dev, &ti);
    if (!hdr_color_) {
        MD_LOG(MD_LOG_WARNING, "DeferredLightingSystem: failed to create HDR texture");
        return;
    }

    // NEAREST + CLAMP for GBuffer / depth reads (no interpolation on G-buffer)
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_NEAREST;
    si.mag_filter     = SDL_GPU_FILTER_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_nearest_ = SDL_CreateGPUSampler(dev, &si);

    // BILINEAR + CLAMP for EVSM moment maps (bilinear improves soft shadow quality)
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_linear_ = SDL_CreateGPUSampler(dev, &si);

    // Fullscreen triangle pipeline
    // frag samplers → set=2 binding 0..5: RT0, RT1, Depth, EVSM0, EVSM1, EVSM2
    // frag UBO      → set=3 binding=0: DeferredAmbientUBO (352B)
    GpuPipeline::Desc d;
    d.vert_path           = "shaders/deferred_lighting.vert";
    d.frag_path           = "shaders/deferred_lighting.frag";
    d.layout.count        = 0;
    d.layout.stride       = 0;
    d.raster.blend_enable = false;
    d.raster.depth_test   = false;
    d.raster.depth_write  = false;
    d.raster.cull_back    = false;
    d.vert_uniform_bufs   = 0;
    d.frag_uniform_bufs   = 1;
    d.frag_samplers       = 6;   // slots 0-2: RT0/RT1/Depth; slots 3-5: EVSM×3
    d.has_depth_target    = false;
    d.depth_only          = false;

    if (!pipeline_.Create(d)) {
        MD_LOG(MD_LOG_WARNING, "DeferredLightingSystem: pipeline create failed");
        SDL_ReleaseGPUTexture(dev, hdr_color_); hdr_color_ = nullptr;
        return;
    }

    MD_LOG(MD_LOG_INFO, "DeferredLightingSystem: ready (%dx%d RGBA16F, EVSM shadows)", w, h);
}

void DeferredLightingSystem::DrawAmbientPass(SDL_GPUCommandBuffer* cmd,
                                              const GBuffer& gbuf,
                                              SDL_GPUTexture* depth_tex) {
    if (!hdr_color_) return;

    SDL_GPUColorTargetInfo ct = {};
    ct.texture     = hdr_color_;
    ct.load_op     = SDL_GPU_LOADOP_CLEAR;
    ct.store_op    = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "DeferredLightingSystem: BeginGPURenderPass failed");
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline_.SDLPipeline());

    // ── slots 0-2: GBuffer RT0, RT1, Depth → SPIR-V set=2 binding=0/1/2 ───────
    {
        SDL_GPUTextureSamplerBinding b[3] = {};
        b[0] = { gbuf.RT0(), sampler_nearest_ };
        b[1] = { gbuf.RT1(), sampler_nearest_ };
        // Depth: use the scene depth texture if provided, otherwise a dummy.
        // Shader guards against out-of-bounds reconstruction (depth == 0 or 1 → skip).
        b[2] = { depth_tex ? depth_tex : gbuf.RT0(),  // fallback: no shadow if no depth
                 sampler_nearest_ };
        SDL_BindGPUFragmentSamplers(pass, 0, b, 3);
    }

    // ── slots 3-5: EVSM moment maps → SPIR-V set=2 binding=3/4/5 ────────────
    const md::EvsmShadow& evsm = md::EvsmShadow::Get();
    const bool evsm_ready = evsm.IsReady() && depth_tex;
    {
        SDL_GPUTexture* fallback = gbuf.RT0();  // safe dummy (never sampled if !evsm_ready)
        SDL_GPUTextureSamplerBinding b[3] = {};
        b[0] = { evsm_ready ? evsm.MomentTex(0) : fallback, sampler_linear_ };
        b[1] = { evsm_ready ? evsm.MomentTex(1) : fallback, sampler_linear_ };
        b[2] = { evsm_ready ? evsm.MomentTex(2) : fallback, sampler_linear_ };
        SDL_BindGPUFragmentSamplers(pass, 3, b, 3);   // slots 3,4,5
    }

    // ── frag UBO slot=0 → SPIR-V set=3 binding=0: DeferredAmbientUBO ────────
    {
        DeferredAmbientUBO ubo = {};

        // Lighting
        ubo.sun_dir[0]  = sun_dir[0];  ubo.sun_dir[1]  = sun_dir[1];
        ubo.sun_dir[2]  = sun_dir[2];  ubo.sun_dir[3]  = 0.0f;
        ubo.sun_color[0] = sun_color[0]; ubo.sun_color[1] = sun_color[1];
        ubo.sun_color[2] = sun_color[2]; ubo.sun_color[3] = 0.0f;
        ubo.ambient_color[0] = ambient_color[0]; ubo.ambient_color[1] = ambient_color[1];
        ubo.ambient_color[2] = ambient_color[2]; ubo.ambient_color[3] = 0.0f;
        ubo.emissive_scale       = emissive_scale;
        ubo.shadow_dist_scalar   = 1.f / 120.f;
        ubo.shadow_height_scalar = 0.f;
        ubo.shadow_height_offset = 1.f;

        // EVSM shadow data
        const ShadowSystem& ss = ShadowSystem::Get();

        // inv_view_proj: reconstructs world position from depth buffer + NDC.
        // Computed from the camera view-projection matrix stored in ShadowSystem
        // (ShadowSystem already has the current camera VP via Update()).
        // We store inv_vp separately: caller sets it via SetInvViewProj().
        memcpy(ubo.inv_view_proj, inv_view_proj_, 64);

        // Per-cascade light-space VP matrices (from ShadowSystem::lightViewProj[])
        for (int k = 0; k < ShadowSystem::NUM_CASCADES; ++k)
            memcpy(ubo.light_vp[k], mat4_ptr(ss.lightViewProj[k]), 64);

        // Cascade far planes (view-space Z) for cascade selection in shader.
        ubo.cascade_splits[0] = ss.cascade_splits[0];  // 20.0m
        ubo.cascade_splits[1] = ss.cascade_splits[1];  // 60.0m
        ubo.cascade_splits[2] = ss.cascade_splits[2];  // 150.0m
        ubo.cascade_splits[3] = 0.f;

        ubo.evsm_warp_c = evsm_ready ? evsm.WarpC() : 40.f;

        SDL_PushGPUFragmentUniformData(cmd, 0, &ubo, sizeof(ubo));
    }

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void DeferredLightingSystem::Shutdown() {
    if (!dev_) return;
    pipeline_.Destroy();
    if (hdr_color_)     { SDL_ReleaseGPUTexture(dev_, hdr_color_);      hdr_color_ = nullptr; }
    if (sampler_nearest_){ SDL_ReleaseGPUSampler(dev_, sampler_nearest_); sampler_nearest_ = nullptr; }
    if (sampler_linear_) { SDL_ReleaseGPUSampler(dev_, sampler_linear_);  sampler_linear_ = nullptr; }
    dev_ = nullptr;
}

} // namespace md
#endif // MD_SDL_GPU
