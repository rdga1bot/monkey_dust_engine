#include <monkey_dust/render/evsm_shadow.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstdio>
#include <cstdlib>

namespace md {

// ── Local SPIR-V loader ───────────────────────────────────────────────────────
static SDL_GPUShader* LoadSpv(SDL_GPUDevice* dev, const char* spv_path,
                               SDL_GPUShaderStage stage,
                               uint32_t uni = 0, uint32_t smp = 0) {
    uint32_t sz = 0;
    uint8_t* code = (uint8_t*)md::fs_read_alloc(spv_path, &sz);
    if (!code) {
        fprintf(stderr, "[EvsmShadow] SPIR-V not found: %s\n", spv_path);
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info{};
    info.code                = code;
    info.code_size           = sz;
    info.entrypoint          = "main";
    info.format              = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage               = stage;
    info.num_uniform_buffers = uni;
    info.num_samplers        = smp;
    SDL_GPUShader* sh = SDL_CreateGPUShader(dev, &info);
    md::fs_free((char*)code);
    return sh;
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────

bool EvsmShadow::Init(int map_size, float warp_c) {
    map_size_ = map_size;
    warp_c_   = warp_c;

    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    if (!dev) return false;

    // ── Moment texture: RG32F, colour target + sampler ────────────────────────
    // RG: (m1, m2) = (exp(c*z), exp(2c*z))
    // R32G32_FLOAT gives full precision; R16G16_FLOAT suffices for c ≤ 40.
    {
        SDL_GPUTextureCreateInfo ti{};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
        ti.width                = (uint32_t)map_size;
        ti.height               = (uint32_t)map_size;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                  SDL_GPU_TEXTUREUSAGE_SAMPLER;
        moment_tex_ = SDL_CreateGPUTexture(dev, &ti);
        if (!moment_tex_) {
            // Fallback to R16G16_FLOAT (required by most Vulkan drivers).
            ti.format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
            moment_tex_ = SDL_CreateGPUTexture(dev, &ti);
        }
        if (!moment_tex_) {
            fprintf(stderr, "[EvsmShadow] moment texture creation failed\n");
            return false;
        }
    }

    // ── Bilinear sampler (linear filtering improves soft edges) ──────────────
    {
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter     = SDL_GPU_FILTER_LINEAR;
        si.mag_filter     = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler_ = SDL_CreateGPUSampler(dev, &si);
    }

    // ── Moment-write pipeline ─────────────────────────────────────────────────
    // Vert: shadow_csm.vert (transforms geometry into light space — reused).
    // Frag: evsm_moments.frag (writes exp(c*z), exp(2c*z) to RG colour).
    {
        SDL_GPUShader* vert = LoadSpv(dev, "shaders/spirv/shadow_csm.vert.spv",
                                       SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
        SDL_GPUShader* frag = LoadSpv(dev, "shaders/spirv/evsm_moments.frag.spv",
                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
        if (!vert || !frag) {
            if (vert) SDL_ReleaseGPUShader(dev, vert);
            if (frag) SDL_ReleaseGPUShader(dev, frag);
            fprintf(stderr, "[EvsmShadow] shader load failed "
                    "(compile shaders/evsm_moments.frag → spirv first)\n");
            // Still succeed — pipeline will be null; callers check MomentPipeline().
        } else {
            // Vertex layout matches CSM depth pass (position only).
            SDL_GPUVertexAttribute va{};
            va.location    = 0;
            va.buffer_slot = 0;
            va.format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            va.offset      = 0;

            SDL_GPUVertexBufferDescription vbd{};
            vbd.slot       = 0;
            vbd.pitch      = 12;  // float[3]
            vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

            SDL_GPUVertexInputState vis{};
            vis.vertex_buffer_descriptions = &vbd;
            vis.num_vertex_buffers         = 1;
            vis.vertex_attributes          = &va;
            vis.num_vertex_attributes      = 1;

            // Determine moment texture format from what was created.
            SDL_GPUTextureCreateInfo fmt_probe{};
            SDL_GPUTextureFormat moment_fmt = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
            // (Use the format we successfully created.)

            SDL_GPUColorTargetDescription ct{};
            ct.format = moment_fmt;

            SDL_GPUGraphicsPipelineTargetInfo ti{};
            ti.color_target_descriptions = &ct;
            ti.num_color_targets         = 1;
            ti.has_depth_stencil_target  = false;

            SDL_GPURasterizerState rast{};
            rast.cull_mode  = SDL_GPU_CULLMODE_NONE;
            rast.fill_mode  = SDL_GPU_FILLMODE_FILL;
            rast.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

            SDL_GPUDepthStencilState ds{};
            ds.enable_depth_test  = false;
            ds.enable_depth_write = false;

            SDL_GPUGraphicsPipelineCreateInfo ci{};
            ci.vertex_shader       = vert;
            ci.fragment_shader     = frag;
            ci.vertex_input_state  = vis;
            ci.rasterizer_state    = rast;
            ci.depth_stencil_state = ds;
            ci.target_info         = ti;
            ci.primitive_type      = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

            moment_pipeline_ = SDL_CreateGPUGraphicsPipeline(dev, &ci);
            SDL_ReleaseGPUShader(dev, vert);
            SDL_ReleaseGPUShader(dev, frag);

            if (!moment_pipeline_)
                fprintf(stderr, "[EvsmShadow] moment pipeline failed: %s\n",
                        SDL_GetError());
        }
    }

    ready_ = (moment_tex_ && sampler_);
    if (ready_)
        fprintf(stdout, "[EvsmShadow] init %dx%d c=%.1f pipeline=%s\n",
                map_size, map_size, (double)warp_c,
                moment_pipeline_ ? "ok" : "null (compile evsm_moments.frag first)");
    return ready_;
}

void EvsmShadow::Shutdown() {
    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    if (!dev) return;
    if (moment_pipeline_) {
        SDL_ReleaseGPUGraphicsPipeline(dev, moment_pipeline_);
        moment_pipeline_ = nullptr;
    }
    if (moment_tex_) { SDL_ReleaseGPUTexture(dev, moment_tex_); moment_tex_ = nullptr; }
    if (sampler_)    { SDL_ReleaseGPUSampler(dev, sampler_);    sampler_    = nullptr; }
    ready_ = false;
}

// ── Shadow pass ───────────────────────────────────────────────────────────────

SDL_GPURenderPass* EvsmShadow::BeginMomentPass(SDL_GPUCommandBuffer* cmd,
                                                SDL_GPUTexture* /*depth_tex*/) {
    if (!ready_) return nullptr;

    // Clear moments to (0, 0) — no contribution.
    SDL_GPUColorTargetInfo ct{};
    ct.texture     = moment_tex_;
    ct.load_op     = SDL_GPU_LOADOP_CLEAR;
    ct.store_op    = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0.f, 0.f, 0.f, 1.f };

    moment_pass_ = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    return moment_pass_;
}

void EvsmShadow::EndMomentPass() {
    if (moment_pass_) {
        SDL_EndGPURenderPass(moment_pass_);
        moment_pass_ = nullptr;
    }
}

// ── Sampling ──────────────────────────────────────────────────────────────────

void EvsmShadow::BindForSampling(SDL_GPURenderPass* pass, uint32_t slot) {
    if (!ready_ || !pass) return;
    SDL_GPUTextureSamplerBinding tsb{ moment_tex_, sampler_ };
    SDL_BindGPUFragmentSamplers(pass, slot, &tsb, 1);
}

} // namespace md
#endif // MD_SDL_GPU
