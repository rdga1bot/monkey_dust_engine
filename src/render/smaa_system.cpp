#ifdef MD_SDL_GPU
#include <monkey_dust/render/smaa_system.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

// std140 UBO shared by all three SMAA passes (texel size).
struct SmaaUBO {
    float rcp_w, rcp_h; float _pad[2];  // vec4 @ 0: 1/width, 1/height
};
static_assert(sizeof(SmaaUBO) == 16);

namespace md {

SMAASystem& SMAASystem::Get() {
    static SMAASystem inst;
    return inst;
}

static bool MakePipe(GpuPipeline& pipe,
                     const char* vert, const char* frag,
                     int frag_samplers, SDL_GPUTextureFormat fmt) {
    GpuPipeline::Desc d;
    d.vert_path          = vert;
    d.frag_path          = frag;
    d.layout.stride      = 0;    // fullscreen triangle via gl_VertexIndex
    d.layout.count       = 0;
    d.raster.blend_enable = false;
    d.raster.depth_test   = false;
    d.raster.depth_write  = false;
    d.raster.cull_back    = false;
    d.vert_uniform_bufs   = 0;
    d.frag_uniform_bufs   = 1;   // SmaaUBO (texel size)
    d.frag_samplers       = (uint32_t)frag_samplers;
    d.has_depth_target    = false;
    d.depth_only          = false;
    d.color_format        = fmt;
    return pipe.Create(d);
}

void SMAASystem::Init(SDL_GPUDevice* dev, int w, int h) {
    if (!RenderTierSystem::Get().IsDeferred()) {
        MD_LOG(MD_LOG_INFO, "SMAASystem: Forward tier — disabled");
        return;
    }
    dev_ = dev; w_ = w; h_ = h;

    // Intermediate RGBA8 render targets
    SDL_GPUTextureCreateInfo ti = {};
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.width                = (Uint32)w;
    ti.height               = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = 1;
    ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    rt_edges_ = SDL_CreateGPUTexture(dev, &ti);
    rt_blend_ = SDL_CreateGPUTexture(dev, &ti);
    if (!rt_edges_ || !rt_blend_) {
        MD_LOG(MD_LOG_WARNING, "SMAASystem: RT create failed");
        return;
    }

    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_LINEAR;
    si.mag_filter     = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linear_sampler_ = SDL_CreateGPUSampler(dev, &si);
    if (!linear_sampler_) {
        MD_LOG(MD_LOG_WARNING, "SMAASystem: sampler create failed");
        return;
    }

    const SDL_GPUTextureFormat RGBA8 = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    const SDL_GPUTextureFormat SWAP  = SDL_GPU_TEXTUREFORMAT_INVALID; // → swapchain

    bool ok = true;
    ok &= MakePipe(edge_pipe_,  "shaders/smaa_edge.vert",  "shaders/smaa_edge.frag",  1, RGBA8);
    ok &= MakePipe(blend_pipe_, "shaders/smaa_blend.vert", "shaders/smaa_blend.frag", 1, RGBA8);
    ok &= MakePipe(final_pipe_, "shaders/smaa_final.vert", "shaders/smaa_final.frag", 2, SWAP);

    if (!ok) {
        MD_LOG(MD_LOG_WARNING, "SMAASystem: one or more pipelines failed");
        return;
    }

    enabled_ = true;
    MD_LOG(MD_LOG_INFO, "SMAASystem: %dx%d 3-pass SMAA ready", w_, h_);
}

static void RunFullscreenPass(GpuPipeline&  pipe,
                               SDL_GPUCommandBuffer* cmd,
                               SDL_GPUTexture*       out_tex,
                               SDL_GPUTexture* const* in_textures,
                               int n_inputs,
                               SDL_GPUSampler*       sampler,
                               const SmaaUBO&        ubo,
                               bool load_output = false) {
    SDL_GPUColorTargetInfo ct = {};
    ct.texture  = out_tex;
    ct.load_op  = load_output ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) return;

    SDL_BindGPUGraphicsPipeline(pass, pipe.SDLPipeline());

    SDL_GPUTextureSamplerBinding bindings[2] = {};
    for (int i = 0; i < n_inputs && i < 2; ++i) {
        bindings[i].texture = in_textures[i];
        bindings[i].sampler = sampler;
    }
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, (Uint32)n_inputs);
    SDL_PushGPUFragmentUniformData(cmd, 0, &ubo, sizeof(ubo));

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);  // fullscreen triangle
    SDL_EndGPURenderPass(pass);
}

void SMAASystem::Render(SDL_GPUCommandBuffer* cmd,
                         SDL_GPUTexture*       input_tex,
                         SDL_GPUTexture*       swapchain_tex) {
    if (!enabled_) return;

    SmaaUBO ubo;
    ubo.rcp_w   = 1.f / (float)w_;
    ubo.rcp_h   = 1.f / (float)h_;
    ubo._pad[0] = 0.f; ubo._pad[1] = 0.f;

    // Pass 1: luma edge detect → rt_edges_
    { SDL_GPUTexture* in[1] = { input_tex };
      RunFullscreenPass(edge_pipe_, cmd, rt_edges_, in, 1, linear_sampler_, ubo); }

    // Pass 2: blend weight → rt_blend_
    { SDL_GPUTexture* in[1] = { rt_edges_ };
      RunFullscreenPass(blend_pipe_, cmd, rt_blend_, in, 1, linear_sampler_, ubo); }

    // Pass 3: final blend → swapchain (LOAD to preserve prior content if needed)
    { SDL_GPUTexture* in[2] = { input_tex, rt_blend_ };
      RunFullscreenPass(final_pipe_, cmd, swapchain_tex, in, 2, linear_sampler_, ubo, true); }
}

void SMAASystem::Shutdown() {
    if (!dev_) return;
    edge_pipe_.Destroy();
    blend_pipe_.Destroy();
    final_pipe_.Destroy();
    if (rt_edges_)      { SDL_ReleaseGPUTexture(dev_, rt_edges_);      rt_edges_       = nullptr; }
    if (rt_blend_)      { SDL_ReleaseGPUTexture(dev_, rt_blend_);      rt_blend_       = nullptr; }
    if (linear_sampler_){ SDL_ReleaseGPUSampler(dev_, linear_sampler_); linear_sampler_ = nullptr; }
    dev_     = nullptr;
    enabled_ = false;
}

} // namespace md
#endif // MD_SDL_GPU
