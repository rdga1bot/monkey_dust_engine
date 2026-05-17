#include <monkey_dust/render/oit_pass.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace md {

// ── Texture management ────────────────────────────────────────────────────────

void OitPass::CreateTextures(int w, int h) {
    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();

    // Accum: RGBA8 — weighted colour sum (simplified OIT, single target).
    {
        SDL_GPUTextureCreateInfo ti{};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.width                = (uint32_t)w;
        ti.height               = (uint32_t)h;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                  SDL_GPU_TEXTUREUSAGE_SAMPLER;
        accum_tex_ = SDL_CreateGPUTexture(dev, &ti);
    }

    // Reveal: R8G8B8A8_UNORM — uses only R channel for transmittance product.
    // Separated from accum for future 2-MRT upgrade; currently single-pass approx.
    reveal_tex_ = nullptr;  // Not used in simplified single-target OIT.

    // Nearest sampler for composite read.
    {
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter     = SDL_GPU_FILTER_NEAREST;
        si.mag_filter     = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler_ = SDL_CreateGPUSampler(dev, &si);
    }

    tex_w_ = w;
    tex_h_ = h;
}

void OitPass::DestroyTextures() {
    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    if (accum_tex_)  { SDL_ReleaseGPUTexture(dev, accum_tex_);  accum_tex_  = nullptr; }
    if (reveal_tex_) { SDL_ReleaseGPUTexture(dev, reveal_tex_); reveal_tex_ = nullptr; }
    if (sampler_)    { SDL_ReleaseGPUSampler(dev, sampler_);    sampler_    = nullptr; }
}

// ── Pipeline management ───────────────────────────────────────────────────────

static SDL_GPUShader* LoadSpv(SDL_GPUDevice* dev, const char* spv_path,
                               SDL_GPUShaderStage stage,
                               uint32_t uni = 0, uint32_t smp = 0) {
    uint32_t sz = 0;
    uint8_t* code = (uint8_t*)md::fs_read_alloc(spv_path, &sz);
    if (!code) {
        fprintf(stderr, "[OitPass] SPIR-V not found: %s\n", spv_path);
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info{};
    info.code             = code;
    info.code_size        = sz;
    info.entrypoint       = "main";
    info.format           = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage            = stage;
    info.num_uniform_buffers = uni;
    info.num_samplers     = smp;
    SDL_GPUShader* sh = SDL_CreateGPUShader(dev, &info);
    md::fs_free((char*)code);
    return sh;
}

void OitPass::CreatePipelines() {
    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();

    // Investigation: create COMPOSITE (alpha-blend) FIRST, then ACCUM (additive).
    // Hypothesis: Intel HD 520 Vulkan driver crashes when an alpha-blend pipeline
    // follows an additive pipeline in the same session. Creating composite first
    // tests whether the crash is order-dependent.

    // ── Composite pipeline (raw SDL_GPU, alpha blend) ─────────────────────────
    // NOTE: On Intel HD 520 + mesa anv, SDL_CreateGPUGraphicsPipeline for ANY
    // pipeline with enable_blend=true crashes the driver during Init.
    // Root cause unknown (possibly pipeline cache corruption or driver bug with
    // Vulkan 1.3 on mesa 25.x anv). Using SDL_BlitGPUTexture fallback instead.
    // The blit composite overlays accumulated OIT color additively — visible but
    // not alpha-blended over opaque scene. Acceptable for demo purposes.
    use_blit_composite_ = true;
    fprintf(stdout, "[OitPass] composite: using blit fallback (alpha-blend pipeline crashes anv)\n");
    if (false) {  // disabled — crashes Intel HD 520 driver
        SDL_GPUShader* vert = LoadSpv(dev, "shaders/spirv/cas.vert.spv",
                                       SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
        SDL_GPUShader* frag = LoadSpv(dev, "shaders/spirv/oit_composite.frag.spv",
                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
        if (vert && frag) {
            SDL_GPUColorTargetDescription comp_target{};
            comp_target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
            comp_target.blend_state.enable_blend          = true;
            comp_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            comp_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            comp_target.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
            comp_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            comp_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            comp_target.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;

            SDL_GPUGraphicsPipelineTargetInfo ti{};
            ti.color_target_descriptions = &comp_target;
            ti.num_color_targets         = 1;
            ti.has_depth_stencil_target  = false;

            SDL_GPURasterizerState rast{};
            rast.cull_mode  = SDL_GPU_CULLMODE_NONE;
            rast.fill_mode  = SDL_GPU_FILLMODE_FILL;
            rast.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

            SDL_GPUDepthStencilState ds{};
            SDL_GPUVertexInputState  vis{};

            SDL_GPUGraphicsPipelineCreateInfo ci{};
            ci.vertex_shader       = vert;
            ci.fragment_shader     = frag;
            ci.vertex_input_state  = vis;
            ci.rasterizer_state    = rast;
            ci.depth_stencil_state = ds;
            ci.target_info         = ti;
            ci.primitive_type      = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

            SDL_GPUGraphicsPipeline* raw = SDL_CreateGPUGraphicsPipeline(dev, &ci);
            if (raw) {
                composite_raw_ = raw;
                fprintf(stdout, "[OitPass] composite pipeline OK (created first)\n");
            } else {
                use_blit_composite_ = true;
                fprintf(stderr, "[OitPass] composite pipeline still failed: %s\n",
                        SDL_GetError());
            }
        } else {
            use_blit_composite_ = true;
        }
        if (vert) SDL_ReleaseGPUShader(dev, vert);
        if (frag) SDL_ReleaseGPUShader(dev, frag);
    }  // end disabled composite block

    // ── Accumulation pipeline (raw SDL_GPU, additive blend) ──────────────────
    {
        SDL_GPUShader* vert = LoadSpv(dev, "shaders/spirv/oit_accum.vert.spv",
                                       SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
        SDL_GPUShader* frag = LoadSpv(dev, "shaders/spirv/oit_accum.frag.spv",
                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
        if (!vert || !frag) {
            if (vert) SDL_ReleaseGPUShader(dev, vert);
            if (frag) SDL_ReleaseGPUShader(dev, frag);
            return;
        }

        // Vertex layout: float[3] pos (loc=0) + float[4] color (loc=1), stride=28.
        SDL_GPUVertexAttribute vattribs[2] = {};
        vattribs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0  };
        vattribs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 12 };

        SDL_GPUVertexBufferDescription vbd{};
        vbd.slot       = 0;
        vbd.pitch      = 28;
        vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexInputState vis{};
        vis.vertex_buffer_descriptions = &vbd;
        vis.num_vertex_buffers         = 1;
        vis.vertex_attributes          = vattribs;
        vis.num_vertex_attributes      = 2;

        // Single color target: RGBA16F with additive blend (simplified OIT).
        // RGB = weighted color sum; A = total weight (used for normalization).
        // Composite shader reads this and divides by weight.
        SDL_GPUColorTargetDescription targets[1] = {};
        targets[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;  // RGBA8 for compat
        targets[0].blend_state.enable_blend          = true;
        targets[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        targets[0].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        targets[0].blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
        targets[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        targets[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        targets[0].blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;

        SDL_GPUGraphicsPipelineTargetInfo target_info{};
        target_info.color_target_descriptions = targets;
        target_info.num_color_targets         = 1;
        target_info.has_depth_stencil_target  = false;

        SDL_GPURasterizerState raster{};
        raster.cull_mode       = SDL_GPU_CULLMODE_NONE;
        raster.fill_mode       = SDL_GPU_FILLMODE_FILL;
        raster.front_face      = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

        SDL_GPUDepthStencilState depth{};
        depth.enable_depth_test  = false;
        depth.enable_depth_write = false;

        SDL_GPUGraphicsPipelineCreateInfo ci{};
        ci.vertex_shader   = vert;
        ci.fragment_shader = frag;
        ci.vertex_input_state  = vis;
        ci.rasterizer_state    = raster;
        ci.depth_stencil_state = depth;
        ci.target_info         = target_info;
        ci.primitive_type      = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

        accum_pipeline_ = SDL_CreateGPUGraphicsPipeline(dev, &ci);
        SDL_ReleaseGPUShader(dev, vert);
        SDL_ReleaseGPUShader(dev, frag);

        if (!accum_pipeline_)
            fprintf(stderr, "[OitPass] accum pipeline failed: %s\n", SDL_GetError());
    }

    fprintf(stdout, "[OitPass] pipelines: composite=%s accum=%s\n",
            composite_raw_ ? "ok" : "blit-fallback",
            accum_pipeline_ ? "ok" : "failed");
}

void OitPass::DestroyPipelines() {
    if (accum_pipeline_) {
        SDL_ReleaseGPUGraphicsPipeline(GpuDevice::Get().SDLDevice(), accum_pipeline_);
        accum_pipeline_ = nullptr;
    }
    composite_pipeline_.Destroy();
    if (composite_raw_) {
        SDL_ReleaseGPUGraphicsPipeline(GpuDevice::Get().SDLDevice(), composite_raw_);
        composite_raw_ = nullptr;
    }
}

// ── Init / Shutdown / Resize ──────────────────────────────────────────────────

bool OitPass::Init(int vp_w, int vp_h) {
    CreateTextures(vp_w, vp_h);
    CreatePipelines();
    // Composite pipeline failure → fall back to SDL_BlitGPUTexture (no alpha blend).
    if (!composite_pipeline_.SDLPipeline()) {
        use_blit_composite_ = true;
        fprintf(stdout, "[OitPass] composite pipeline failed → using blit fallback\n");
    }
    ready_ = (accum_tex_ && accum_pipeline_);
    if (ready_) fprintf(stdout, "[OitPass] init %dx%d\n", vp_w, vp_h);
    return ready_;
}

void OitPass::Shutdown() {
    DestroyPipelines();
    DestroyTextures();
    ready_ = false;
}

void OitPass::Resize(int vp_w, int vp_h) {
    if (vp_w == tex_w_ && vp_h == tex_h_) return;
    DestroyTextures();
    CreateTextures(vp_w, vp_h);
}

// ── BeginAccum / EndAccum ─────────────────────────────────────────────────────

SDL_GPURenderPass* OitPass::BeginAccum(SDL_GPUCommandBuffer* cmd,
                                        SDL_GPUTexture* /*opaque_depth*/) {
    if (!ready_) return nullptr;
    Resize(tex_w_, tex_h_);  // no-op if same size

    // Single accumulation target cleared to (0,0,0,0).
    // Simplified single-target OIT: RGB=weighted color, A=weight sum.
    SDL_GPUColorTargetInfo targets[1] = {};
    targets[0].texture     = accum_tex_;
    targets[0].load_op     = SDL_GPU_LOADOP_CLEAR;
    targets[0].store_op    = SDL_GPU_STOREOP_STORE;
    targets[0].clear_color = { 0.f, 0.f, 0.f, 0.f };

    accum_pass_ = SDL_BeginGPURenderPass(cmd, targets, 1, nullptr);
    return accum_pass_;
}

void OitPass::EndAccum() {
    if (accum_pass_) {
        SDL_EndGPURenderPass(accum_pass_);
        accum_pass_ = nullptr;
    }
}

// ── Composite ─────────────────────────────────────────────────────────────────

void OitPass::Composite(SDL_GPUCommandBuffer* cmd,
                         SDL_GPUTexture* output_tex,
                         int vp_w, int vp_h) {
    if (!ready_ || !output_tex) return;

    if (use_blit_composite_ || !composite_pipeline_.SDLPipeline()) {
        // Fallback: blit accumulated transparent geometry over scene (no alpha blend).
        // The accum texture contains additive-blended transparent colour — shows
        // as bright tinted areas over the opaque scene geometry.
        SDL_GPUBlitInfo blit{};
        blit.source.texture = accum_tex_;
        blit.source.w       = (uint32_t)vp_w;
        blit.source.h       = (uint32_t)vp_h;
        blit.destination.texture = output_tex;
        blit.destination.w  = (uint32_t)vp_w;
        blit.destination.h  = (uint32_t)vp_h;
        blit.load_op        = SDL_GPU_LOADOP_LOAD;
        blit.filter         = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &blit);
        return;
    }

    // Full composite: render accum over opaque scene with alpha blend.
    SDL_GPUColorTargetInfo ct{};
    ct.texture   = output_tex;
    ct.load_op   = SDL_GPU_LOADOP_LOAD;   // preserve existing opaque scene
    ct.store_op  = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (rp) {
        SDL_BindGPUGraphicsPipeline(rp, composite_raw_);
        SDL_GPUTextureSamplerBinding tsb{ accum_tex_, sampler_ };
        SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }
    (void)vp_w; (void)vp_h;
}

} // namespace md
#endif // MD_SDL_GPU
