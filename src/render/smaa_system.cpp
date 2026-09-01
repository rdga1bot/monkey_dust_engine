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
    const SDL_GPUTextureUsageFlags rt_usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                                             | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    bool rt_ok = rt_edges_.Init(w, h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, rt_usage);
    rt_ok &= rt_blend_.Init(w, h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, rt_usage);
    if (!rt_ok) {
        MD_LOG(MD_LOG_WARNING, "SMAASystem: RT create failed");
        return;
    }

    GpuSamplerDesc sdesc;
    sdesc.min_filter = GpuSamplerDesc::Filter::LINEAR;
    sdesc.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sdesc.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sdesc.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sdesc.gen_mipmap = false;
    if (!linear_sampler_.Init(sdesc)) {
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
    // GpuCommandBuffer (own-pass HAL wrapper, docs/HAL_CLOSURE_INVENTORY.md
    // M1 pilot) -- this function already opens+closes its own pass locally,
    // so it fits the wrapper's owning design directly (unlike callers that
    // draw into a pass someone else opened -- see GpuPassView instead).
    GpuCommandBuffer::ColorPassDesc cpd;
    cpd.cmd            = cmd;
    cpd.color_tex[0]      = out_tex;
    // Match the original SDL_GPUColorTargetInfo ct = {} zero-init exactly:
    // clear_color={0,0,0,0}, not ColorPassDesc's own default of {0,0,0,1}.
    cpd.clear_color[0] = 0.f; cpd.clear_color[1] = 0.f;
    cpd.clear_color[2] = 0.f; cpd.clear_color[3] = 0.f;
    cpd.load_color     = load_output;

    GpuCommandBuffer cb;
    cb.BeginColorPass(cpd);
    if (!cb.SDLPass()) return;

    cb.BindPipeline(&pipe);

    SDL_GPUTextureSamplerBinding bindings[2] = {};
    for (int i = 0; i < n_inputs && i < 2; ++i) {
        bindings[i].texture = in_textures[i];
        bindings[i].sampler = sampler;
    }
    cb.BindFragmentSamplers(0, bindings, (Uint32)n_inputs);
    cb.PushFragmentUniforms(0, &ubo, sizeof(ubo));

    cb.Draw(3);  // fullscreen triangle -- Draw() hardcodes instance_count=1, matches original
    cb.EndPass();
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
      RunFullscreenPass(edge_pipe_, cmd, rt_edges_.SDLTexture(), in, 1, linear_sampler_.SDLSampler(), ubo); }

    // Pass 2: blend weight → rt_blend_
    { SDL_GPUTexture* in[1] = { rt_edges_.SDLTexture() };
      RunFullscreenPass(blend_pipe_, cmd, rt_blend_.SDLTexture(), in, 1, linear_sampler_.SDLSampler(), ubo); }

    // Pass 3: final blend → swapchain (LOAD to preserve prior content if needed)
    { SDL_GPUTexture* in[2] = { input_tex, rt_blend_.SDLTexture() };
      RunFullscreenPass(final_pipe_, cmd, swapchain_tex, in, 2, linear_sampler_.SDLSampler(), ubo, true); }
}

void SMAASystem::Shutdown() {
    if (!dev_) return;
    edge_pipe_.Destroy();
    blend_pipe_.Destroy();
    final_pipe_.Destroy();
    rt_edges_.Shutdown(); rt_blend_.Shutdown(); linear_sampler_.Shutdown();
    dev_     = nullptr;
    enabled_ = false;
}

} // namespace md
#endif // MD_SDL_GPU
