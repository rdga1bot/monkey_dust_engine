#include <monkey_dust/render/evsm_shadow.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstdio>

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

bool EvsmShadow::Init(int num_cascades, int map_size, float warp_c) {
    num_cascades_ = (num_cascades > 0 && num_cascades <= NUM_CASCADES)
                    ? num_cascades : NUM_CASCADES;
    map_size_ = map_size;
    warp_c_   = warp_c;

    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    if (!dev) return false;

    // ── Moment textures: RG32F, one per cascade ───────────────────────────────
    // R32G32_FLOAT: full precision; fallback R16G16_FLOAT for drivers that lack it.
    for (int k = 0; k < num_cascades_; ++k) {
        SDL_GPUTextureCreateInfo ti{};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
        ti.width                = (uint32_t)map_size;
        ti.height               = (uint32_t)map_size;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                  SDL_GPU_TEXTUREUSAGE_SAMPLER;
        moment_tex_[k] = SDL_CreateGPUTexture(dev, &ti);
        if (!moment_tex_[k]) {
            ti.format      = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
            moment_tex_[k] = SDL_CreateGPUTexture(dev, &ti);
        }
        if (!moment_tex_[k]) {
            fprintf(stderr, "[EvsmShadow] moment texture[%d] creation failed\n", k);
            return false;
        }
    }

    // ── Bilinear sampler (linear filtering on RG32F is safe and improves softness) ─
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
    // Vert: shadow_csm.vert — same light-space transform as CSM depth pass.
    // Frag: evsm_moments.frag — writes (exp(c*z), exp(2c*z)) to RG color target.
    {
        SDL_GPUShader* vert = LoadSpv(dev, "shaders/spirv/shadow_csm.vert.spv",
                                       SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
        SDL_GPUShader* frag = LoadSpv(dev, "shaders/spirv/evsm_moments.frag.spv",
                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
        if (!vert || !frag) {
            if (vert) SDL_ReleaseGPUShader(dev, vert);
            if (frag) SDL_ReleaseGPUShader(dev, frag);
            fprintf(stderr, "[EvsmShadow] shader load failed\n");
        } else {
            // Vertex layout: position only (float3, stride 24 = pos+norm same as CSM).
            SDL_GPUVertexAttribute va{};
            va.location    = 0;
            va.buffer_slot = 0;
            va.format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            va.offset      = 0;

            SDL_GPUVertexBufferDescription vbd{};
            vbd.slot       = 0;
            vbd.pitch      = 24;
            vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

            SDL_GPUVertexInputState vis{};
            vis.vertex_buffer_descriptions = &vbd;
            vis.num_vertex_buffers         = 1;
            vis.vertex_attributes          = &va;
            vis.num_vertex_attributes      = 1;

            SDL_GPUColorTargetDescription ct{};
            ct.format = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;

            SDL_GPUGraphicsPipelineTargetInfo tgt{};
            tgt.color_target_descriptions = &ct;
            tgt.num_color_targets         = 1;
            tgt.has_depth_stencil_target  = false;

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
            ci.target_info         = tgt;
            ci.primitive_type      = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

            moment_pipeline_ = SDL_CreateGPUGraphicsPipeline(dev, &ci);
            SDL_ReleaseGPUShader(dev, vert);
            SDL_ReleaseGPUShader(dev, frag);

            if (!moment_pipeline_)
                fprintf(stderr, "[EvsmShadow] moment pipeline failed: %s\n",
                        SDL_GetError());
        }
    }

    ready_ = (moment_tex_[0] && sampler_);

    // ── VBfA R-3: blur temp textures + compute pipelines ─────────────────────
    // blur_tmp_: ping-pong target for H pass; must be COMPUTE_STORAGE_WRITE + SAMPLER.
    // moment_tex_ also needs COMPUTE_STORAGE_WRITE so V pass can write back to it.
    // NOTE: moment_tex_ was already created with COLOR_TARGET|SAMPLER above.
    //       SDL3 GPU allows mixing usage flags — just add COMPUTE_STORAGE_WRITE.
    for (int k = 0; k < num_cascades_; ++k) {
        SDL_GPUTextureCreateInfo ti{};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
        ti.width                = (uint32_t)map_size;
        ti.height               = (uint32_t)map_size;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE
                                | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        blur_tmp_[k] = SDL_CreateGPUTexture(dev, &ti);
        if (!blur_tmp_[k]) {
            fprintf(stderr, "[EvsmShadow] blur_tmp[%d] create failed: %s\n",
                    k, SDL_GetError());
        }
    }
    {
        GpuComputePipeline::Desc bd{};
        bd.glsl_path                      = "shaders/shadow_blur_h.comp";
        bd.num_samplers                   = 1;   // src_moments (set=0, binding=0)
        bd.num_readwrite_storage_textures = 1;   // dst_moments (set=1, binding=0)
        bd.threadcount_x                  = 16;
        bd.threadcount_y                  = 16;
        bd.threadcount_z                  = 1;
        blur_h_cs_.Create(bd);

        bd.glsl_path = "shaders/shadow_blur_v.comp";
        blur_v_cs_.Create(bd);
    }
    blur_ready_ = blur_tmp_[0] &&
                  blur_h_cs_.SDLComputePipeline() &&
                  blur_v_cs_.SDLComputePipeline();

    fprintf(stdout, "[EvsmShadow] %d cascades, %dx%d, c=%.1f, pipeline=%s, blur=%s\n",
            num_cascades_, map_size, map_size, (double)warp_c,
            moment_pipeline_ ? "ok" : "null",
            blur_ready_       ? "ok" : "null");
    return ready_;
}

void EvsmShadow::Shutdown() {
    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    if (!dev) return;
    if (moment_pipeline_) {
        SDL_ReleaseGPUGraphicsPipeline(dev, moment_pipeline_);
        moment_pipeline_ = nullptr;
    }
    for (int k = 0; k < NUM_CASCADES; ++k) {
        if (moment_tex_[k]) {
            SDL_ReleaseGPUTexture(dev, moment_tex_[k]);
            moment_tex_[k] = nullptr;
        }
        if (blur_tmp_[k]) {
            SDL_ReleaseGPUTexture(dev, blur_tmp_[k]);
            blur_tmp_[k] = nullptr;
        }
    }
    blur_h_cs_.Destroy();
    blur_v_cs_.Destroy();
    if (sampler_) { SDL_ReleaseGPUSampler(dev, sampler_); sampler_ = nullptr; }
    ready_      = false;
    blur_ready_ = false;
}

// ── VBfA R-3: Gaussian blur ────────────────���──────────────────────────────────
// 2-pass separable 5×5: H(moment_tex → blur_tmp) then V(blur_tmp → moment_tex).
// Dispatch 1 thread per pixel; local_size = 16×16 → groups = map_size/16.
void EvsmShadow::ApplyBlur(SDL_GPUCommandBuffer* cmd) {
    if (!cmd || !blur_ready_) return;
    const uint32_t groups = (uint32_t)map_size_ / 16u;

    for (int k = 0; k < num_cascades_; ++k) {
        if (!moment_tex_[k] || !blur_tmp_[k]) continue;

        // ── H pass: sample moment_tex_[k], write blur_tmp_[k] ────────────────
        {
            SDL_GPUStorageTextureReadWriteBinding rw{};
            rw.texture = blur_tmp_[k];
            rw.cycle   = false;
            SDL_GPUComputePass* cp =
                SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
            if (cp) {
                SDL_BindGPUComputePipeline(cp, blur_h_cs_.SDLComputePipeline());
                SDL_GPUTextureSamplerBinding tsb{ moment_tex_[k], sampler_ };
                SDL_BindGPUComputeSamplers(cp, 0, &tsb, 1);
                SDL_DispatchGPUCompute(cp, groups, groups, 1);
                SDL_EndGPUComputePass(cp);
            }
        }
        // ── V pass: sample blur_tmp_[k], write moment_tex_[k] ────��───────────
        {
            SDL_GPUStorageTextureReadWriteBinding rw{};
            rw.texture = moment_tex_[k];
            rw.cycle   = false;
            SDL_GPUComputePass* cp =
                SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
            if (cp) {
                SDL_BindGPUComputePipeline(cp, blur_v_cs_.SDLComputePipeline());
                SDL_GPUTextureSamplerBinding tsb{ blur_tmp_[k], sampler_ };
                SDL_BindGPUComputeSamplers(cp, 0, &tsb, 1);
                SDL_DispatchGPUCompute(cp, groups, groups, 1);
                SDL_EndGPUComputePass(cp);
            }
        }
    }
}

// ── Shadow pass ───────────────────────────────────────────────────────────────

SDL_GPURenderPass* EvsmShadow::BeginMomentPass(SDL_GPUCommandBuffer* cmd, int cascade) {
    if (!ready_ || cascade < 0 || cascade >= num_cascades_) return nullptr;

    SDL_GPUColorTargetInfo ct{};
    ct.texture     = moment_tex_[cascade];
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

void EvsmShadow::BindArrayForSampling(SDL_GPURenderPass* pass, uint32_t slot) {
    if (!ready_ || !pass) return;
    // Bind all cascades at consecutive slots: slot+0, slot+1, slot+2.
    // Shader declares: layout(set=2, binding=slot+k) uniform sampler2D evsm_k;
    // NOTE: Intel HD 520 has frag sampler limits — keep total frag samplers ≤ 4.
    SDL_GPUTextureSamplerBinding tsb[NUM_CASCADES];
    for (int k = 0; k < num_cascades_; ++k)
        tsb[k] = { moment_tex_[k], sampler_ };
    SDL_BindGPUFragmentSamplers(pass, slot, tsb, (uint32_t)num_cascades_);
}

} // namespace md
#endif // MD_SDL_GPU
