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

    // Accum: RGBA16F — weighted colour sum + weight (McGuire & Bavoil 2013, MRT0).
    {
        SDL_GPUTextureCreateInfo ti{};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ti.width                = (uint32_t)w;
        ti.height               = (uint32_t)h;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                  SDL_GPU_TEXTUREUSAGE_SAMPLER;
        accum_tex_ = SDL_CreateGPUTexture(dev, &ti);
    }

    // Reveal: R8G8B8A8_UNORM — revealage Π(1-αᵢ), cleared to {1,1,1,1} (MRT1).
    // ZERO/ONE_MINUS_SRC_ALPHA blend on R channel gives the product each draw.
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
        reveal_tex_ = SDL_CreateGPUTexture(dev, &ti);
    }

    // Merged: R8G8B8A8_UNORM — compute composite output (scene + OIT blended).
    // Needs COMPUTE_STORAGE_WRITE for imageStore in oit_composite_manual.comp.
    // Also COLOR_TARGET|SAMPLER to allow later blit to swap.
    {
        SDL_GPUTextureCreateInfo ti{};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.width                = (uint32_t)w;
        ti.height               = (uint32_t)h;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                  SDL_GPU_TEXTUREUSAGE_SAMPLER      |
                                  SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        merged_tex_ = SDL_CreateGPUTexture(dev, &ti);
    }


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
    if (merged_tex_) { SDL_ReleaseGPUTexture(dev, merged_tex_); merged_tex_ = nullptr; }
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

    // ── Manual-blend composite via COMPUTE ───────────────────────────────────
    // Graphics pipeline with 2+ fragment samplers crashes Intel HD 520 / mesa anv.
    // Compute path uses SDL_BindGPUComputeSamplers (same as SSAO, known to work).
    // oit_composite_manual.comp: set=0 samplers(accum,scene,reveal) → set=1 image(merged).
    {
        GpuComputePipeline::Desc cd{};
        cd.glsl_path                   = "shaders/oit_composite_manual.comp";
        cd.num_samplers                = 3;  // u_accum + u_scene + u_reveal (set=0, binding=0..2)
        cd.num_readwrite_storage_textures = 1;  // u_output (set=1, binding=0)
        cd.threadcount_x               = 8;
        cd.threadcount_y               = 8;
        cd.threadcount_z               = 1;
        if (composite_compute_.Create(cd)) {
            fprintf(stdout, "[OitPass] composite compute pipeline OK\n");
        } else {
            fprintf(stderr, "[OitPass] composite compute pipeline failed — blit fallback\n");
            use_blit_composite_ = true;
        }
    }

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

        // 2-MRT: accum (RGBA16F, ONE/ONE additive) + reveal (R8G8B8A8, ZERO/ONE_MINUS_SRC_ALPHA).
        // MRT0 accumulates weighted colour sum and weight.
        // MRT1 accumulates revealage product Π(1-αᵢ) via multiplicative blend.
        SDL_GPUColorTargetDescription targets[2] = {};
        // MRT0 — accum RGBA16F, additive blend
        targets[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        targets[0].blend_state.enable_blend          = true;
        targets[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        targets[0].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        targets[0].blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
        targets[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        targets[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        targets[0].blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
        // MRT1 — reveal R8G8B8A8, ZERO/ONE_MINUS_SRC_ALPHA → dst *= (1 - src.a) each draw
        targets[1].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        targets[1].blend_state.enable_blend          = true;
        targets[1].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        targets[1].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        targets[1].blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
        targets[1].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        targets[1].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        targets[1].blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;

        SDL_GPUGraphicsPipelineTargetInfo target_info{};
        target_info.color_target_descriptions = targets;
        target_info.num_color_targets         = 2;
        target_info.has_depth_stencil_target  = true;
        target_info.depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

        SDL_GPURasterizerState raster{};
        raster.cull_mode       = SDL_GPU_CULLMODE_NONE;
        raster.fill_mode       = SDL_GPU_FILLMODE_FILL;
        raster.front_face      = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

        // Depth test against opaque scene — transparent quads behind walls are clipped.
        // No depth write: transparent objects don't modify the depth buffer.
        SDL_GPUDepthStencilState depth{};
        depth.enable_depth_test  = true;
        depth.enable_depth_write = false;
        depth.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

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
            composite_compute_.SDLComputePipeline() ? "compute" : "blit-fallback",
            accum_pipeline_ ? "ok" : "failed");
}

void OitPass::DestroyPipelines() {
    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    if (accum_pipeline_) {
        SDL_ReleaseGPUGraphicsPipeline(dev, accum_pipeline_);
        accum_pipeline_ = nullptr;
    }
    composite_compute_.Destroy();
    composite_pipeline_.Destroy();
    if (composite_raw_) {
        SDL_ReleaseGPUGraphicsPipeline(dev, composite_raw_);
        composite_raw_ = nullptr;
    }
}

// ── Init / Shutdown / Resize ──────────────────────────────────────────────────

bool OitPass::Init(int vp_w, int vp_h) {
    CreateTextures(vp_w, vp_h);
    CreatePipelines();
    ready_ = (accum_tex_ && reveal_tex_ && accum_pipeline_);
    if (ready_) {
        fprintf(stdout, "[OitPass] init %dx%d  composite=%s\n", vp_w, vp_h,
                composite_compute_.SDLComputePipeline() ? "compute" : "blit-fallback");
    }
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
                                        SDL_GPUTexture* opaque_depth) {
    if (!ready_) return nullptr;
    Resize(tex_w_, tex_h_);  // no-op if same size

    // 2-MRT: accum cleared to (0,0,0,0); reveal cleared to (1,1,1,1).
    // Reveal starts at 1 so Π(1-αᵢ) product is correct when no fragments drawn.
    SDL_GPUColorTargetInfo targets[2] = {};
    targets[0].texture     = accum_tex_;
    targets[0].load_op     = SDL_GPU_LOADOP_CLEAR;
    targets[0].store_op    = SDL_GPU_STOREOP_STORE;
    targets[0].clear_color = { 0.f, 0.f, 0.f, 0.f };
    targets[1].texture     = reveal_tex_;
    targets[1].load_op     = SDL_GPU_LOADOP_CLEAR;
    targets[1].store_op    = SDL_GPU_STOREOP_STORE;
    targets[1].clear_color = { 1.f, 1.f, 1.f, 1.f };

    // Depth test against opaque scene so transparent quads are clipped by walls.
    // load_op=LOAD preserves scene depth; store_op=DONT_CARE since depth_write=false.
    SDL_GPUDepthStencilTargetInfo depth_info{};
    depth_info.texture   = opaque_depth;
    depth_info.load_op   = SDL_GPU_LOADOP_LOAD;
    depth_info.store_op  = SDL_GPU_STOREOP_DONT_CARE;

    accum_pass_ = SDL_BeginGPURenderPass(cmd, targets, 2,
                                          opaque_depth ? &depth_info : nullptr);
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
                         SDL_GPUTexture* scene_tex,
                         SDL_GPUTexture* output_tex,
                         int vp_w, int vp_h) {
    if (!ready_ || !output_tex) return;

    // Compute composite path: dispatch oit_composite_manual.comp to blend
    // accum + scene → merged_tex_, then blit merged_tex_ → output_tex (swap).
    // Compute avoids the graphics pipeline path that crashes Intel HD 520 / mesa anv
    // with 2+ fragment samplers. Same pattern as SSAOSystem.
    SDL_GPUComputePipeline* comp = composite_compute_.SDLComputePipeline();
    if (comp && merged_tex_ && scene_tex) {
        // Open compute pass with merged_tex_ as readwrite storage texture.
        SDL_GPUStorageTextureReadWriteBinding rw_tex{};
        rw_tex.texture = merged_tex_;
        rw_tex.cycle   = false;

        SDL_GPUComputePass* cpass = SDL_BeginGPUComputePass(cmd, &rw_tex, 1, nullptr, 0);
        if (cpass) {
            SDL_BindGPUComputePipeline(cpass, comp);

            // Bind accum(0), scene(1), reveal(2) as compute samplers (set=0).
            SDL_GPUTextureSamplerBinding samplers[3] = {
                { accum_tex_,  sampler_ },
                { scene_tex,   sampler_ },
                { reveal_tex_, sampler_ },
            };
            SDL_BindGPUComputeSamplers(cpass, 0, samplers, 3);

            // Dispatch one thread per pixel (8×8 tiles).
            uint32_t gx = ((uint32_t)vp_w  + 7u) / 8u;
            uint32_t gy = ((uint32_t)vp_h + 7u) / 8u;
            SDL_DispatchGPUCompute(cpass, gx, gy, 1);
            SDL_EndGPUComputePass(cpass);
        }

        // Blit merged_tex_ → output_tex (no pipeline required).
        SDL_GPUBlitInfo blit{};
        blit.source.texture      = merged_tex_;
        blit.source.w            = (uint32_t)vp_w;
        blit.source.h            = (uint32_t)vp_h;
        blit.destination.texture = output_tex;
        blit.destination.w       = (uint32_t)vp_w;
        blit.destination.h       = (uint32_t)vp_h;
        blit.load_op             = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter              = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &blit);
        return;
    }

    // Blit fallback: additive overlay (no correct alpha blend, but no crash).
    SDL_GPUBlitInfo blit{};
    blit.source.texture      = accum_tex_;
    blit.source.w            = (uint32_t)vp_w;
    blit.source.h            = (uint32_t)vp_h;
    blit.destination.texture = output_tex;
    blit.destination.w       = (uint32_t)vp_w;
    blit.destination.h       = (uint32_t)vp_h;
    blit.load_op             = SDL_GPU_LOADOP_LOAD;
    blit.filter              = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(cmd, &blit);
}

} // namespace md
#endif // MD_SDL_GPU
