#ifdef MD_SDL_GPU
#include <monkey_dust/render/deferred_lighting.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/platform/md_log.h>

// ── Fragment UBO (must match DeferredAmbientUBO std140 in deferred_lighting.frag) ──
struct DeferredAmbientUBO {
    float sun_dir[4];        // vec4 @  0 (xyz=sun→surface, w=unused)
    float sun_color[4];      // vec4 @ 16
    float ambient_color[4];  // vec4 @ 32
    float emissive_scale;    // float @ 48
    float _pad[3];           // align to 64 B
};
static_assert(sizeof(DeferredAmbientUBO) == 64);

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

    // NEAREST + CLAMP sampler for GBuffer reads (same as GBuffer::Sampler())
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_NEAREST;
    si.mag_filter     = SDL_GPU_FILTER_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(dev, &si);

    // Fullscreen triangle pipeline — no vertex buffer, no depth, opaque blend
    GpuPipeline::Desc d;
    d.vert_path            = "shaders/deferred_lighting.vert";
    d.frag_path            = "shaders/deferred_lighting.frag";
    d.layout.count         = 0;   // no vertex attributes — gl_VertexIndex only
    d.layout.stride        = 0;
    d.raster.blend_enable  = false;
    d.raster.depth_test    = false;
    d.raster.depth_write   = false;
    d.raster.cull_back     = false;
    d.vert_uniform_bufs    = 0;
    d.frag_uniform_bufs    = 1;   // set=1 binding=0 DeferredAmbientUBO
    d.frag_samplers        = 2;   // RT0, RT1
    d.has_depth_target     = false;
    d.depth_only           = false;

    if (!pipeline_.Create(d)) {
        MD_LOG(MD_LOG_WARNING, "DeferredLightingSystem: pipeline create failed");
        SDL_ReleaseGPUTexture(dev, hdr_color_); hdr_color_ = nullptr;
        return;
    }

    MD_LOG(MD_LOG_INFO, "DeferredLightingSystem: ready (%dx%d RGBA16F)", w, h);
}

void DeferredLightingSystem::DrawAmbientPass(SDL_GPUCommandBuffer* cmd,
                                              const GBuffer& gbuf) {
    if (!hdr_color_) return;

    SDL_GPUColorTargetInfo ct = {};
    ct.texture  = hdr_color_;
    ct.load_op  = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "DeferredLightingSystem: SDL_BeginGPURenderPass failed");
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline_.SDLPipeline());

    // Bind GBuffer RT0 + RT1 as fragment samplers
    SDL_GPUTextureSamplerBinding bindings[2] = {};
    bindings[0].texture = gbuf.RT0(); bindings[0].sampler = sampler_;
    bindings[1].texture = gbuf.RT1(); bindings[1].sampler = sampler_;
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);

    // Push fragment UBO
    DeferredAmbientUBO ubo = {};
    ubo.sun_dir[0]        = sun_dir[0];       ubo.sun_dir[1] = sun_dir[1];
    ubo.sun_dir[2]        = sun_dir[2];       ubo.sun_dir[3] = 0.0f;
    ubo.sun_color[0]      = sun_color[0];     ubo.sun_color[1] = sun_color[1];
    ubo.sun_color[2]      = sun_color[2];     ubo.sun_color[3] = 0.0f;
    ubo.ambient_color[0]  = ambient_color[0]; ubo.ambient_color[1] = ambient_color[1];
    ubo.ambient_color[2]  = ambient_color[2]; ubo.ambient_color[3] = 0.0f;
    ubo.emissive_scale    = emissive_scale;
    SDL_PushGPUFragmentUniformData(cmd, 0, &ubo, sizeof(ubo));

    // Draw fullscreen triangle (3 vertices, no VBO)
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    SDL_EndGPURenderPass(pass);
}

void DeferredLightingSystem::Shutdown() {
    if (!dev_) return;
    pipeline_.Destroy();
    if (hdr_color_) { SDL_ReleaseGPUTexture(dev_, hdr_color_); hdr_color_ = nullptr; }
    if (sampler_)   { SDL_ReleaseGPUSampler(dev_, sampler_);   sampler_   = nullptr; }
    dev_ = nullptr;
}

} // namespace md

#endif // MD_SDL_GPU
