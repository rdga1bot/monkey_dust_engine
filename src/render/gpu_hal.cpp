#include <monkey_dust/render/gpu_hal.h>

#if defined(MD_OPENGL43_ENABLED) || defined(MD_SDL_GPU)
#include <monkey_dust/platform/md_log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "glad.h"

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#endif

// ── Common helpers ─────────────────────────────────────────────────────────────

static char* ReadTextFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return nullptr; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

// stb_image declarations — implementation provided by Raylib (rtextures.c).
// Included here (outside backend guards) so both OpenGL and SDL_GPU paths can call stbi_load.
#include "stb_image.h"

#ifdef MD_SDL_GPU

static void* ReadBinaryFile(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    void* buf = malloc((size_t)len);
    if (!buf) { fclose(f); return nullptr; }
    fread(buf, 1, (size_t)len, f);
    fclose(f);
    *out_size = (size_t)len;
    return buf;
}

// Derive SPIR-V path: "shaders/pbr.vert" → "shaders/spirv/pbr.vert.spv"
static void MakeSpvPath(char* out, size_t out_sz, const char* glsl_path) {
    const char* slash = strrchr(glsl_path, '/');
    const char* name  = slash ? slash + 1 : glsl_path;
    snprintf(out, out_sz, "shaders/spirv/%s.spv", name);
}

static SDL_GPUVertexElementFormat ToSDLFmt(GpuAttribFmt f) {
    switch (f) {
    case GpuAttribFmt::F1:        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case GpuAttribFmt::F2:        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case GpuAttribFmt::F3:        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case GpuAttribFmt::F4:        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case GpuAttribFmt::U8x4_NORM: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
}

static SDL_GPUPrimitiveType ToSDLPrim(GpuTopology t) {
    switch (t) {
    case GpuTopology::POINTS: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
    case GpuTopology::LINES:  return SDL_GPU_PRIMITIVETYPE_LINELIST;
    default:                  return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    }
}

static SDL_GPUBlendFactor ToSDLBlend(GpuBlendFactor f) {
    switch (f) {
    case GpuBlendFactor::ZERO:                return SDL_GPU_BLENDFACTOR_ZERO;
    case GpuBlendFactor::ONE:                 return SDL_GPU_BLENDFACTOR_ONE;
    case GpuBlendFactor::SRC_COLOR:           return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case GpuBlendFactor::ONE_MINUS_SRC_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case GpuBlendFactor::SRC_ALPHA:           return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case GpuBlendFactor::ONE_MINUS_SRC_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case GpuBlendFactor::DST_ALPHA:           return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case GpuBlendFactor::ONE_MINUS_DST_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case GpuBlendFactor::DST_COLOR:           return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case GpuBlendFactor::ONE_MINUS_DST_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

static uint32_t MipLevels(int w, int h) {
    uint32_t n = 1;
    int dim = (w > h) ? w : h;
    while (dim > 1) { dim >>= 1; ++n; }
    return n;
}

static SDL_GPUFilter ToSDLFilter(GpuSamplerDesc::Filter f) {
    return (f == GpuSamplerDesc::Filter::NEAREST) ? SDL_GPU_FILTER_NEAREST
                                                   : SDL_GPU_FILTER_LINEAR;
}

static SDL_GPUSamplerAddressMode ToSDLWrap(GpuSamplerDesc::Wrap w) {
    return (w == GpuSamplerDesc::Wrap::CLAMP_TO_EDGE) ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                       : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
}

static SDL_GPUSampler* CreateSDLSampler(SDL_GPUDevice* dev, const GpuSamplerDesc& s) {
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter       = ToSDLFilter(s.min_filter);
    info.mag_filter       = ToSDLFilter(s.mag_filter);
    info.mipmap_mode      = (s.min_filter == GpuSamplerDesc::Filter::LINEAR_MIPMAP)
                            ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                            : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u   = ToSDLWrap(s.wrap_s);
    info.address_mode_v   = ToSDLWrap(s.wrap_t);
    info.address_mode_w   = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.min_lod          = 0.0f;
    info.max_lod          = s.gen_mipmap ? 1000.0f : 0.0f;
    return SDL_CreateGPUSampler(dev, &info);
}

static SDL_GPUShader* LoadSpvShader(SDL_GPUDevice* dev,
                                    const char*    spv_path,
                                    SDL_GPUShaderStage stage,
                                    uint32_t num_uniform_bufs,
                                    uint32_t num_storage_bufs,
                                    uint32_t num_samplers) {
    size_t code_size = 0;
    void*  code      = ReadBinaryFile(spv_path, &code_size);
    if (!code) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] SPIR-V not found: %s", spv_path);
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info = {};
    info.code              = (const Uint8*)code;
    info.code_size         = code_size;
    info.entrypoint        = "main";
    info.format            = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage             = stage;
    info.num_uniform_buffers = num_uniform_bufs;
    info.num_storage_buffers = num_storage_bufs;
    info.num_samplers      = num_samplers;
    SDL_GPUShader* sh = SDL_CreateGPUShader(dev, &info);
    free(code);
    if (!sh) MD_LOG(MD_LOG_WARNING, "[GpuPipeline] SDL_CreateGPUShader failed: %s", SDL_GetError());
    return sh;
}

#endif // MD_SDL_GPU

// ── OpenGL helpers ─────────────────────────────────────────────────────────────

#ifdef MD_OPENGL43_ENABLED

static void ApplyAttribFormat(const GpuVertexAttrib& a) {
    switch (a.fmt) {
    case GpuAttribFmt::F1:
        glVertexAttribFormat(a.location, 1, GL_FLOAT, GL_FALSE, a.offset); break;
    case GpuAttribFmt::F2:
        glVertexAttribFormat(a.location, 2, GL_FLOAT, GL_FALSE, a.offset); break;
    case GpuAttribFmt::F3:
        glVertexAttribFormat(a.location, 3, GL_FLOAT, GL_FALSE, a.offset); break;
    case GpuAttribFmt::F4:
        glVertexAttribFormat(a.location, 4, GL_FLOAT, GL_FALSE, a.offset); break;
    case GpuAttribFmt::U8x4_NORM:
        glVertexAttribFormat(a.location, 4, GL_UNSIGNED_BYTE, GL_TRUE, a.offset); break;
    }
    glVertexAttribBinding(a.location, 0);
    glEnableVertexAttribArray(a.location);
}

static GLenum ToGL(GpuTopology t) {
    switch (t) {
    case GpuTopology::TRIANGLES: return GL_TRIANGLES;
    case GpuTopology::POINTS:    return GL_POINTS;
    case GpuTopology::LINES:     return GL_LINES;
    }
    return GL_TRIANGLES;
}

static GLenum ToGLBlend(GpuBlendFactor f) {
    switch (f) {
    case GpuBlendFactor::ZERO:                return GL_ZERO;
    case GpuBlendFactor::ONE:                 return GL_ONE;
    case GpuBlendFactor::SRC_COLOR:           return GL_SRC_COLOR;
    case GpuBlendFactor::ONE_MINUS_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
    case GpuBlendFactor::SRC_ALPHA:           return GL_SRC_ALPHA;
    case GpuBlendFactor::ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case GpuBlendFactor::DST_ALPHA:           return GL_DST_ALPHA;
    case GpuBlendFactor::ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case GpuBlendFactor::DST_COLOR:           return GL_DST_COLOR;
    case GpuBlendFactor::ONE_MINUS_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
    }
    return GL_ONE;
}

#endif // MD_OPENGL43_ENABLED

// ── GpuPipeline ───────────────────────────────────────────────────────────────

bool GpuPipeline::Create(const Desc& desc) {
    if (!desc.vert_path || !desc.frag_path) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] Create: null shader paths");
        return false;
    }
    raster_ = desc.raster;

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (!dev) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] SDL_GPU not ready");
        return false;
    }

    char spv_vert[256], spv_frag[256];
    MakeSpvPath(spv_vert, sizeof(spv_vert), desc.vert_path);
    MakeSpvPath(spv_frag, sizeof(spv_frag), desc.frag_path);

    SDL_GPUShader* vert_sh = LoadSpvShader(dev, spv_vert,
        SDL_GPU_SHADERSTAGE_VERTEX,
        desc.vert_uniform_bufs, desc.vert_storage_bufs, 0);
    SDL_GPUShader* frag_sh = LoadSpvShader(dev, spv_frag,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        desc.frag_uniform_bufs, desc.frag_storage_bufs, desc.frag_samplers);

    if (!vert_sh || !frag_sh) {
        if (vert_sh) SDL_ReleaseGPUShader(dev, vert_sh);
        if (frag_sh) SDL_ReleaseGPUShader(dev, frag_sh);
        return false;
    }

    // Vertex input state
    SDL_GPUVertexBufferDescription vbds[2] = {};
    vbds[0].slot               = 0;
    vbds[0].pitch              = desc.layout.stride;
    vbds[0].input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbds[0].instance_step_rate = 0;
    const bool has_inst = (desc.layout.inst_stride > 0 && desc.layout.inst_count > 0);
    if (has_inst) {
        vbds[1].slot               = 1;
        vbds[1].pitch              = desc.layout.inst_stride;
        vbds[1].input_rate         = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        vbds[1].instance_step_rate = 0; // SDL_GPU requires 0; INSTANCE input_rate controls stepping
    }

    SDL_GPUVertexAttribute vattribs[16] = {};
    uint32_t total_attribs = 0;
    for (uint32_t i = 0; i < desc.layout.count; ++i, ++total_attribs) {
        vattribs[total_attribs].location    = desc.layout.attribs[i].location;
        vattribs[total_attribs].buffer_slot = 0;
        vattribs[total_attribs].format      = ToSDLFmt(desc.layout.attribs[i].fmt);
        vattribs[total_attribs].offset      = desc.layout.attribs[i].offset;
    }
    if (has_inst) {
        for (uint32_t i = 0; i < desc.layout.inst_count; ++i, ++total_attribs) {
            vattribs[total_attribs].location    = desc.layout.inst_attribs[i].location;
            vattribs[total_attribs].buffer_slot = 1;
            vattribs[total_attribs].format      = ToSDLFmt(desc.layout.inst_attribs[i].fmt);
            vattribs[total_attribs].offset      = desc.layout.inst_attribs[i].offset;
        }
    }

    SDL_GPUVertexInputState vertex_input = {};
    vertex_input.vertex_buffer_descriptions = (desc.layout.stride > 0) ? vbds : nullptr;
    vertex_input.num_vertex_buffers         = (desc.layout.stride > 0) ? (has_inst ? 2u : 1u) : 0u;
    vertex_input.vertex_attributes          = (total_attribs > 0) ? vattribs : nullptr;
    vertex_input.num_vertex_attributes      = total_attribs;

    // Color target (uses swapchain format); skipped for depth_only passes.
    SDL_GPUColorTargetDescription color_target = {};
    if (!desc.depth_only) {
        SDL_Window* win = md::GpuDevice::Get().Window();
        color_target.format = SDL_GetGPUSwapchainTextureFormat(dev, win);
        if (desc.raster.blend_enable) {
            color_target.blend_state.enable_blend          = true;
            color_target.blend_state.src_color_blendfactor = ToSDLBlend(desc.raster.src_factor);
            color_target.blend_state.dst_color_blendfactor = ToSDLBlend(desc.raster.dst_factor);
            color_target.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
            color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            color_target.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
            color_target.blend_state.color_write_mask      = 0xF;
        }
    }

    SDL_GPUGraphicsPipelineTargetInfo target_info = {};
    target_info.color_target_descriptions = desc.depth_only ? nullptr : &color_target;
    target_info.num_color_targets         = desc.depth_only ? 0u : 1u;
    if (desc.has_depth_target || desc.depth_only) {
        target_info.depth_stencil_format       = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        target_info.has_depth_stencil_target   = true;
    }

    SDL_GPUGraphicsPipelineCreateInfo ci = {};
    ci.vertex_shader          = vert_sh;
    ci.fragment_shader        = frag_sh;
    ci.vertex_input_state     = vertex_input;
    ci.primitive_type         = ToSDLPrim(desc.raster.topology);
    ci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode  = desc.raster.cull_back
                                     ? SDL_GPU_CULLMODE_BACK
                                     : SDL_GPU_CULLMODE_NONE;
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    ci.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    ci.depth_stencil_state.enable_depth_test  = desc.raster.depth_test  ? true : false;
    ci.depth_stencil_state.enable_depth_write = desc.raster.depth_write ? true : false;
    ci.target_info = target_info;

    sdl_pipeline_ = SDL_CreateGPUGraphicsPipeline(dev, &ci);

    // Shaders are consumed by the pipeline; release immediately.
    SDL_ReleaseGPUShader(dev, vert_sh);
    SDL_ReleaseGPUShader(dev, frag_sh);

    if (!sdl_pipeline_) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] SDL_CreateGPUGraphicsPipeline failed: %s",
               SDL_GetError());
        return false;
    }
    MD_LOG(MD_LOG_INFO, "[GpuPipeline] SDL_GPU pipeline created: %s / %s",
           desc.vert_path, desc.frag_path);
    return true;

#elif defined(MD_OPENGL43_ENABLED)

    shader_ = MdLoadShader(desc.vert_path, desc.frag_path);
    if (!shader_.id) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] MdLoadShader failed: %s", desc.vert_path);
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    for (uint32_t i = 0; i < desc.layout.count; ++i)
        ApplyAttribFormat(desc.layout.attribs[i]);
    glBindVertexArray(0);

    return true;

#else
    (void)desc;
    return false;
#endif
}

void GpuPipeline::Destroy() {
#ifdef MD_SDL_GPU
    if (sdl_pipeline_) {
        SDL_ReleaseGPUGraphicsPipeline(md::GpuDevice::Get().SDLDevice(), sdl_pipeline_);
        sdl_pipeline_ = nullptr;
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    MdUnloadShader(shader_);
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
#endif
}

#if defined(MD_OPENGL43_ENABLED) || defined(MD_SDL_GPU)
int GpuPipeline::UniformLoc(const char* name) const {
#ifdef MD_OPENGL43_ENABLED
    return MdGetLoc(shader_, name);
#else
    (void)name; return -1;
#endif
}
#endif

// ── GpuVertexBuffer ───────────────────────────────────────────────────────────

void GpuVertexBuffer::Init(uint32_t max_vertices, uint32_t vertex_stride) {
    stride_ = vertex_stride;
#ifdef MD_SDL_GPU
    sdl_size_ = max_vertices * vertex_stride;
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();

    SDL_GPUBufferCreateInfo buf_info = {};
    buf_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buf_info.size  = sdl_size_;
    sdl_buf_ = SDL_CreateGPUBuffer(dev, &buf_info);
    if (!sdl_buf_)
        MD_LOG(MD_LOG_WARNING, "[GpuVertexBuffer] SDL_CreateGPUBuffer failed: %s", SDL_GetError());

    SDL_GPUTransferBufferCreateInfo tbuf_info = {};
    tbuf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbuf_info.size  = sdl_size_;
    sdl_transfer_ = SDL_CreateGPUTransferBuffer(dev, &tbuf_info);
    if (!sdl_transfer_)
        MD_LOG(MD_LOG_WARNING, "[GpuVertexBuffer] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
#elif defined(MD_OPENGL43_ENABLED)
    ring_.Init(max_vertices * vertex_stride);
#endif
}

void GpuVertexBuffer::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_buf_)      { SDL_ReleaseGPUBuffer(dev, sdl_buf_);                sdl_buf_      = nullptr; }
    if (sdl_transfer_) { SDL_ReleaseGPUTransferBuffer(dev, sdl_transfer_);   sdl_transfer_ = nullptr; }
    sdl_size_ = 0;
#elif defined(MD_OPENGL43_ENABLED)
    ring_.Shutdown();
#endif
    stride_ = 0;
}

void* GpuVertexBuffer::MapWrite() {
#ifdef MD_SDL_GPU
    if (!sdl_transfer_) return nullptr;
    return SDL_MapGPUTransferBuffer(md::GpuDevice::Get().SDLDevice(),
                                    sdl_transfer_, true /*cycle*/);
#elif defined(MD_OPENGL43_ENABLED)
    return ring_.MapWrite();
#else
    return nullptr;
#endif
}

void GpuVertexBuffer::Unmap() {
#ifdef MD_SDL_GPU
    if (sdl_transfer_)
        SDL_UnmapGPUTransferBuffer(md::GpuDevice::Get().SDLDevice(), sdl_transfer_);
#elif defined(MD_OPENGL43_ENABLED)
    ring_.Unmap();
#endif
}

#ifdef MD_SDL_GPU
void GpuVertexBuffer::Upload(SDL_GPUCommandBuffer* cmd) {
    if (!cmd || !sdl_buf_ || !sdl_transfer_) return;
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = sdl_transfer_;
    src.offset          = 0;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = sdl_buf_;
    dst.offset = 0;
    dst.size   = sdl_size_;
    SDL_UploadToGPUBuffer(pass, &src, &dst, true /*cycle*/);
    SDL_EndGPUCopyPass(pass);
}
#endif

void GpuVertexBuffer::Advance() {
#ifdef MD_OPENGL43_ENABLED
    ring_.Advance();
#endif
    // SDL_GPU: cycle=true in MapWrite/Upload handles versioning — no explicit advance needed.
}

// ── GpuCommandBuffer ─────────────────────────────────────────────────────────

void GpuCommandBuffer::BindPipeline(GpuPipeline* p) {
    pipeline_ = p;
    if (!p) return;

#ifdef MD_SDL_GPU
    if (sdl_cmd_) {
        if (sdl_pass_) SDL_BindGPUGraphicsPipeline(sdl_pass_, p->sdl_pipeline_);
        return;  // dual-backend: skip GL path when SDL_GPU command buffer is active
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    MdUseShader(p->shader_);

    const GpuRasterState& r = p->raster_;
    if (r.blend_enable) {
        glEnable(GL_BLEND);
        glBlendFunc(ToGLBlend(r.src_factor), ToGLBlend(r.dst_factor));
    }
    if (!r.depth_test)  glDisable(GL_DEPTH_TEST);
    if (!r.depth_write) glDepthMask(GL_FALSE);
    if (!r.cull_back)   glDisable(GL_CULL_FACE);
    if (r.point_size)   glEnable(GL_PROGRAM_POINT_SIZE);
#endif
}

void GpuCommandBuffer::BindVertexBuffer(GpuVertexBuffer* buf) {
#ifdef MD_SDL_GPU
    if (sdl_cmd_) {
        if (sdl_pass_ && buf && buf->sdl_buf_) {
            SDL_GPUBufferBinding binding = {};
            binding.buffer = buf->sdl_buf_;
            binding.offset = 0;
            SDL_BindGPUVertexBuffers(sdl_pass_, 0, &binding, 1);
        }
        return;
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    if (!pipeline_ || !buf) return;
    glBindVertexArray(pipeline_->vao_);
    glBindVertexBuffer(0, buf->ring_.GLBuffer(), buf->ring_.GLOffset(), (GLsizei)buf->stride_);
#else
    (void)buf;
#endif
}

void GpuCommandBuffer::SetUniformMat4(int loc, const float* m16) {
#ifdef MD_OPENGL43_ENABLED
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m16);
#else
    (void)loc; (void)m16;
#endif
}

void GpuCommandBuffer::SetUniformVec3(int loc, const float* v3) {
#ifdef MD_OPENGL43_ENABLED
    if (loc >= 0) glUniform3fv(loc, 1, v3);
#else
    (void)loc; (void)v3;
#endif
}

void GpuCommandBuffer::Draw(uint32_t vertex_count, uint32_t first_vertex) {
#ifdef MD_SDL_GPU
    if (sdl_cmd_) {
        if (sdl_pass_) SDL_DrawGPUPrimitives(sdl_pass_, vertex_count, 1, first_vertex, 0);
        return;
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    if (!pipeline_) return;
    glDrawArrays(ToGL(pipeline_->raster_.topology),
                 (GLint)first_vertex, (GLsizei)vertex_count);
#else
    (void)vertex_count; (void)first_vertex;
#endif
}

void GpuCommandBuffer::EndPass() {
#ifdef MD_SDL_GPU
    if (sdl_cmd_) {
        if (sdl_pass_) { SDL_EndGPURenderPass(sdl_pass_); sdl_pass_ = nullptr; }
        sdl_cmd_ = nullptr;
        pipeline_ = nullptr;
        return;  // dual-backend: skip GL path when SDL_GPU command buffer is active
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    if (pipeline_) {
        glBindVertexArray(0);
        const GpuRasterState& r = pipeline_->raster_;
        if (r.blend_enable)  glDisable(GL_BLEND);
        if (!r.depth_test)   glEnable(GL_DEPTH_TEST);
        if (!r.depth_write)  glDepthMask(GL_TRUE);
        if (!r.cull_back)    glEnable(GL_CULL_FACE);
        if (r.point_size)    glDisable(GL_PROGRAM_POINT_SIZE);
        MdStopShader();
    }
#endif
    pipeline_ = nullptr;
}

// ── GpuCommandBuffer + GpuRenderPass — SDL_GPU paths ─────────────────────────

#ifdef MD_SDL_GPU

void GpuCommandBuffer::BeginColorPass(const ColorPassDesc& desc) {
    sdl_cmd_ = desc.cmd;

    SDL_GPUColorTargetInfo color_info = {};
    color_info.texture     = desc.color_tex;
    color_info.clear_color = { desc.clear_color[0], desc.clear_color[1],
                               desc.clear_color[2], desc.clear_color[3] };
    color_info.load_op  = desc.load_color ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    color_info.cycle    = false;

    if (desc.depth_tex) {
        SDL_GPUDepthStencilTargetInfo depth_info = {};
        depth_info.texture          = desc.depth_tex;
        depth_info.clear_depth      = desc.clear_depth;
        depth_info.load_op          = desc.load_depth ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        depth_info.store_op         = SDL_GPU_STOREOP_STORE;
        depth_info.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth_info.cycle            = false;
        sdl_pass_ = SDL_BeginGPURenderPass(sdl_cmd_, &color_info, 1, &depth_info);
    } else {
        sdl_pass_ = SDL_BeginGPURenderPass(sdl_cmd_, &color_info, 1, nullptr);
    }
}

void GpuCommandBuffer::BindFragmentSamplers(uint32_t first_slot,
                                             const SDL_GPUTextureSamplerBinding* bindings,
                                             uint32_t count) {
    if (sdl_pass_)
        SDL_BindGPUFragmentSamplers(sdl_pass_, first_slot, bindings, count);
}

void GpuCommandBuffer::PushVertexUniforms(uint32_t slot, const void* data, uint32_t size_bytes) {
    if (sdl_cmd_)
        SDL_PushGPUVertexUniformData(sdl_cmd_, slot, data, size_bytes);
}

void GpuCommandBuffer::PushFragmentUniforms(uint32_t slot, const void* data, uint32_t size_bytes) {
    if (sdl_cmd_)
        SDL_PushGPUFragmentUniformData(sdl_cmd_, slot, data, size_bytes);
}

void GpuRenderPass::BeginDepthOnly(SDL_GPUCommandBuffer* cmd, const DepthDesc& desc) {
    SDL_GPUDepthStencilTargetInfo depth_info = {};
    depth_info.texture          = desc.target->SDLTexture();
    depth_info.clear_depth      = desc.clear_depth;
    depth_info.load_op          = SDL_GPU_LOADOP_CLEAR;
    depth_info.store_op         = SDL_GPU_STOREOP_STORE;
    depth_info.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_info.cycle            = false;
    sdl_pass_  = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth_info);
    cull_front_ = desc.cull_front; // stored for symmetry; SDL_GPU cull is pipeline-configured
}

#endif // MD_SDL_GPU

// ── GpuRenderPass::BeginColor (dual-backend) ─────────────────────────────────

void GpuRenderPass::BeginColor(const ColorDesc& desc) {
#ifdef MD_SDL_GPU
    if (desc.cmd) {
        uint32_t sw = 0, sh = 0;
        SDL_GPUTexture* color_tex =
            md::GpuDevice::Get().AcquireSwapchainTexture(desc.cmd, &sw, &sh);
        if (color_tex) {
            SDL_GPUColorTargetInfo color_info = {};
            color_info.texture     = color_tex;
            color_info.clear_color = { desc.clear[0], desc.clear[1],
                                       desc.clear[2], desc.clear[3] };
            color_info.load_op  = SDL_GPU_LOADOP_CLEAR;
            color_info.store_op = SDL_GPU_STOREOP_STORE;
            color_info.cycle    = false;

            if (desc.depth) {
                SDL_GPUDepthStencilTargetInfo depth_info = {};
                depth_info.texture          = desc.depth->SDLTexture();
                depth_info.clear_depth      = desc.clear_depth;
                depth_info.load_op          = SDL_GPU_LOADOP_CLEAR;
                depth_info.store_op         = SDL_GPU_STOREOP_STORE;
                depth_info.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
                depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                depth_info.cycle            = false;
                sdl_pass_ = SDL_BeginGPURenderPass(desc.cmd, &color_info, 1, &depth_info);
            } else {
                sdl_pass_ = SDL_BeginGPURenderPass(desc.cmd, &color_info, 1, nullptr);
            }
        }
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    glGetIntegerv(GL_VIEWPORT, saved_vp_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(desc.clear[0], desc.clear[1], desc.clear[2], desc.clear[3]);
    glClearDepthf(desc.clear_depth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#else
    (void)desc;
#endif
}

// ── GpuComputePipeline + GpuComputePass (dual-backend) ────────────────────────

bool GpuComputePipeline::Create(const Desc& desc) {
    if (!desc.glsl_path) {
        MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] Create: null glsl_path");
        return false;
    }

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        char spv[256];
        MakeSpvPath(spv, sizeof(spv), desc.glsl_path);
        size_t code_size = 0;
        void*  code = ReadBinaryFile(spv, &code_size);
        if (code) {
            SDL_GPUComputePipelineCreateInfo ci = {};
            ci.code                           = (const Uint8*)code;
            ci.code_size                      = code_size;
            ci.entrypoint                     = "main";
            ci.format                         = SDL_GPU_SHADERFORMAT_SPIRV;
            ci.num_uniform_buffers            = desc.num_uniform_buffers;
            ci.num_readonly_storage_buffers   = desc.num_readonly_storage_buffers;
            ci.num_readwrite_storage_buffers  = desc.num_readwrite_storage_buffers;
            ci.num_readonly_storage_textures  = desc.num_readonly_storage_textures;
            ci.num_readwrite_storage_textures = desc.num_readwrite_storage_textures;
            ci.num_samplers                   = desc.num_samplers;
            ci.threadcount_x                  = desc.threadcount_x;
            ci.threadcount_y                  = desc.threadcount_y;
            ci.threadcount_z                  = desc.threadcount_z;
            sdl_pipeline_ = SDL_CreateGPUComputePipeline(dev, &ci);
            free(code);
            if (!sdl_pipeline_)
                MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] SDL_CreateGPUComputePipeline %s: %s",
                       desc.glsl_path, SDL_GetError());
            else
                MD_LOG(MD_LOG_INFO, "[GpuComputePipeline] SDL_GPU pipeline: %s", desc.glsl_path);
        } else {
            MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] SPIR-V not found: %s", spv);
        }
    }
#endif

#ifdef MD_OPENGL43_ENABLED
    {
        char* src = ReadTextFile(desc.glsl_path);
        if (!src) {
            MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] file not found: %s", desc.glsl_path);
            return false;
        }
        GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(sh, 1, (const GLchar**)&src, nullptr);
        glCompileShader(sh);
        free(src);

        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(sh, 512, nullptr, log);
            MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] compile error %s: %s", desc.glsl_path, log);
            glDeleteShader(sh);
            return false;
        }
        program_ = glCreateProgram();
        glAttachShader(program_, sh);
        glLinkProgram(program_);
        glDeleteShader(sh);

        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetProgramInfoLog(program_, 512, nullptr, log);
            MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] link error %s: %s", desc.glsl_path, log);
            glDeleteProgram(program_);
            program_ = 0;
            return false;
        }
    }
    return true;
#else
    return sdl_pipeline_ != nullptr;
#endif
}

void GpuComputePipeline::Destroy() {
#ifdef MD_SDL_GPU
    if (sdl_pipeline_) {
        SDL_ReleaseGPUComputePipeline(md::GpuDevice::Get().SDLDevice(), sdl_pipeline_);
        sdl_pipeline_ = nullptr;
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    if (program_) { glDeleteProgram(program_); program_ = 0; }
#endif
}

int GpuComputePipeline::UniformLoc(const char* name) const {
#ifdef MD_OPENGL43_ENABLED
    return program_ ? (int)glGetUniformLocation(program_, name) : -1;
#else
    (void)name;
    return -1; // SDL_GPU: use PushUniforms instead
#endif
}

// ── GpuComputePass ────────────────────────────────────────────────────────────

void GpuComputePass::Begin(GpuComputePipeline* pipeline, const StorageBindings& bindings) {
    pipeline_ = pipeline;
#ifdef MD_SDL_GPU
    sdl_cmd_ = bindings.cmd;
    if (sdl_cmd_ && pipeline && pipeline->sdl_pipeline_) {
        sdl_pass_ = SDL_BeginGPUComputePass(
            sdl_cmd_,
            nullptr, 0,
            bindings.rw_buffers, bindings.num_rw_buffers
        );
        if (sdl_pass_) {
            SDL_BindGPUComputePipeline(sdl_pass_, pipeline->sdl_pipeline_);
            if (bindings.num_ro_buffers > 0)
                SDL_BindGPUComputeStorageBuffers(
                    sdl_pass_, 0, bindings.ro_buffers, bindings.num_ro_buffers);
        }
    }
#else
    (void)bindings;
#endif
#ifdef MD_OPENGL43_ENABLED
    if (pipeline_ && pipeline_->program_) glUseProgram(pipeline_->program_);
#endif
}

void GpuComputePass::SetUniformFloat(int loc, float v) {
#ifdef MD_OPENGL43_ENABLED
    if (loc >= 0) glUniform1f(loc, v);
#else
    (void)loc; (void)v;
#endif
}

void GpuComputePass::SetUniformInt(int loc, int v) {
#ifdef MD_OPENGL43_ENABLED
    if (loc >= 0) glUniform1i(loc, v);
#else
    (void)loc; (void)v;
#endif
}

void GpuComputePass::SetUniformVec3(int loc, const float* v3) {
#ifdef MD_OPENGL43_ENABLED
    if (loc >= 0) glUniform3fv(loc, 1, v3);
#else
    (void)loc; (void)v3;
#endif
}

void GpuComputePass::SetUniformVec4Array(int loc, const float* v4, int count) {
#ifdef MD_OPENGL43_ENABLED
    if (loc >= 0) glUniform4fv(loc, count, v4);
#else
    (void)loc; (void)v4; (void)count;
#endif
}

#ifdef MD_SDL_GPU
void GpuComputePass::PushUniforms(uint32_t slot, const void* data, uint32_t size_bytes) {
    if (sdl_cmd_)
        SDL_PushGPUComputeUniformData(sdl_cmd_, slot, data, size_bytes);
}
#endif

void GpuComputePass::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
#ifdef MD_SDL_GPU
    if (sdl_pass_) SDL_DispatchGPUCompute(sdl_pass_, gx, gy, gz);
#endif
#ifdef MD_OPENGL43_ENABLED
    glDispatchCompute(gx, gy, gz);
#endif
}

void GpuComputePass::End(uint32_t barrier_flags) {
#ifdef MD_SDL_GPU
    if (sdl_pass_) {
        SDL_EndGPUComputePass(sdl_pass_);
        sdl_pass_ = nullptr;
    }
    sdl_cmd_ = nullptr;
#endif
#ifdef MD_OPENGL43_ENABLED
    glUseProgram(0);
    GLbitfield bits = 0;
    if (barrier_flags & BARRIER_STORAGE) bits |= GL_SHADER_STORAGE_BARRIER_BIT;
    if (barrier_flags & BARRIER_COMMAND)  bits |= GL_COMMAND_BARRIER_BIT;
    if (bits) glMemoryBarrier(bits);
#else
    (void)barrier_flags;
#endif
    pipeline_ = nullptr;
}

// ── GpuDepthTexture ───────────────────────────────────────────────────────────

#if defined(MD_OPENGL43_ENABLED) || defined(MD_SDL_GPU)

void GpuDepthTexture::Init(int w, int h, bool shadow_border) {
    w_ = w; h_ = h;
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();

    SDL_GPUTextureCreateInfo ti = {};
    ti.type                  = SDL_GPU_TEXTURETYPE_2D;
    ti.format                = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    ti.usage                 = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                               SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width                 = (Uint32)w;
    ti.height                = (Uint32)h;
    ti.layer_count_or_depth  = 1;
    ti.num_levels            = 1;
    sdl_tex_ = SDL_CreateGPUTexture(dev, &ti);
    if (!sdl_tex_) {
        MD_LOG(MD_LOG_WARNING, "[GpuDepthTexture] SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return;
    }
    // SDL3 has no CLAMP_TO_BORDER — use CLAMP_TO_EDGE (minor shadow-edge artefact).
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_LINEAR;
    si.mag_filter     = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.min_lod        = 0.0f;
    si.max_lod        = 0.0f;
    sdl_sampler_ = SDL_CreateGPUSampler(dev, &si);
#endif
#ifdef MD_OPENGL43_ENABLED
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (shadow_border) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = { 1.f, 1.f, 1.f, 1.f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#else
    (void)shadow_border;
#endif
}

void GpuDepthTexture::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_sampler_) { SDL_ReleaseGPUSampler(dev, sdl_sampler_); sdl_sampler_ = nullptr; }
    if (sdl_tex_)     { SDL_ReleaseGPUTexture(dev, sdl_tex_);     sdl_tex_     = nullptr; }
#endif
#ifdef MD_OPENGL43_ENABLED
    if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (tex_) { glDeleteTextures(1,    &tex_); tex_ = 0; }
#endif
    w_ = h_ = 0;
}

void GpuDepthTexture::Bind(uint32_t unit) const {
#ifdef MD_OPENGL43_ENABLED
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex_);
#else
    (void)unit; // SDL_GPU: binding via SDL_BindGPUFragmentSamplers in render pass (Step 6)
#endif
}

// ── GpuRenderPass ─────────────────────────────────────────────────────────────

#ifdef MD_OPENGL43_ENABLED
void GpuRenderPass::BeginDepthOnly(const DepthDesc& desc) {
    cull_front_ = desc.cull_front;
    glGetIntegerv(GL_VIEWPORT, saved_vp_);
    glBindFramebuffer(GL_FRAMEBUFFER, desc.target->FBO());
    glViewport(0, 0, desc.target->Width(), desc.target->Height());
    glClearDepthf(desc.clear_depth);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (cull_front_) glCullFace(GL_FRONT);
}
#endif // MD_OPENGL43_ENABLED

void GpuRenderPass::End() {
#ifdef MD_SDL_GPU
    if (sdl_pass_) {
        SDL_EndGPURenderPass(sdl_pass_);
        sdl_pass_ = nullptr;
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    if (cull_front_) glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(saved_vp_[0], saved_vp_[1], saved_vp_[2], saved_vp_[3]);
#endif
    cull_front_ = false;
}

// ── GpuDrawIndexedIndirect ────────────────────────────────────────────────────

#ifdef MD_OPENGL43_ENABLED
void GpuDrawIndexedIndirect(unsigned int indirect_buf_id, uint32_t draw_count) {
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buf_id);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
                                (GLsizei)draw_count, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}
#endif // MD_OPENGL43_ENABLED

// ── GpuStaticBuffer ───────────────────────────────────────────────────────────

void GpuStaticBuffer::Init(unsigned int target, const void* data, uint32_t size) {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();

    // Map GL target → SDL_GPU buffer usage.
    // 0x8893 = GL_ELEMENT_ARRAY_BUFFER value (avoid GL header dependency in SDL-only builds).
    SDL_GPUBufferUsageFlags usage = (target == 0x8893u)
                                    ? SDL_GPU_BUFFERUSAGE_INDEX
                                    : SDL_GPU_BUFFERUSAGE_VERTEX;
    (void)target;

    SDL_GPUBufferCreateInfo buf_info = {};
    buf_info.usage = usage;
    buf_info.size  = size;
    sdl_buf_ = SDL_CreateGPUBuffer(dev, &buf_info);
    if (!sdl_buf_) {
        MD_LOG(MD_LOG_WARNING, "[GpuStaticBuffer] SDL_CreateGPUBuffer failed: %s", SDL_GetError());
        return;
    }

    // One-shot upload: staging transfer buffer → device buffer.
    SDL_GPUTransferBufferCreateInfo tbuf_info = {};
    tbuf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbuf_info.size  = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(dev, &tbuf_info);
    if (!transfer) {
        MD_LOG(MD_LOG_WARNING, "[GpuStaticBuffer] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return;
    }

    void* map = SDL_MapGPUTransferBuffer(dev, transfer, false);
    if (map) { memcpy(map, data, size); SDL_UnmapGPUTransferBuffer(dev, transfer); }

    SDL_GPUCommandBuffer* cmd  = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass*      pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = transfer;
    src.offset          = 0;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = sdl_buf_;
    dst.offset = 0;
    dst.size   = size;
    SDL_UploadToGPUBuffer(pass, &src, &dst, false /*no cycle — one shot*/);
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(dev, transfer); // staging no longer needed
#endif

#ifdef MD_OPENGL43_ENABLED
    glGenBuffers(1, &gl_buf_);
    glBindBuffer(target, gl_buf_);
    glBufferData(target, (GLsizeiptr)size, data, GL_STATIC_DRAW);
    glBindBuffer(target, 0);
#endif
}

void GpuStaticBuffer::Shutdown() {
#ifdef MD_SDL_GPU
    if (sdl_buf_) {
        SDL_ReleaseGPUBuffer(md::GpuDevice::Get().SDLDevice(), sdl_buf_);
        sdl_buf_ = nullptr;
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    if (gl_buf_) { glDeleteBuffers(1, &gl_buf_); gl_buf_ = 0; }
#endif
}

void GpuStaticBuffer::Bind(unsigned int target) const {
#ifdef MD_OPENGL43_ENABLED
    glBindBuffer(target, gl_buf_);
#else
    (void)target;
#endif
}

void GpuStaticBuffer::BindVertex(uint32_t slot, uint32_t stride, uint64_t offset) const {
#ifdef MD_OPENGL43_ENABLED
    glBindVertexBuffer((GLuint)slot, gl_buf_, (GLintptr)offset, (GLsizei)stride);
#else
    (void)slot; (void)stride; (void)offset;
#endif
}

// ── GpuTexture ────────────────────────────────────────────────────────────────
// (stb_image.h included above, outside backend guards)

static GLenum ToGLFilter(GpuSamplerDesc::Filter f, bool is_min) {
    switch (f) {
    case GpuSamplerDesc::Filter::NEAREST:       return GL_NEAREST;
    case GpuSamplerDesc::Filter::LINEAR:        return GL_LINEAR;
    case GpuSamplerDesc::Filter::LINEAR_MIPMAP: return is_min ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    }
    return GL_LINEAR;
}

static GLenum ToGLWrap(GpuSamplerDesc::Wrap w) {
    return (w == GpuSamplerDesc::Wrap::CLAMP_TO_EDGE) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
}

void GpuTexture::ApplySampler(const GpuSamplerDesc& s) const {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)ToGLFilter(s.min_filter, true));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)ToGLFilter(s.mag_filter, false));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     (GLint)ToGLWrap(s.wrap_s));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     (GLint)ToGLWrap(s.wrap_t));
    if (s.gen_mipmap) glGenerateMipmap(GL_TEXTURE_2D);
}

bool GpuTexture::InitFromFile(const char* path, const GpuSamplerDesc& s) {
    int ch;
    stbi_set_flip_vertically_on_load(s.flip_v ? 1 : 0);
    uint8_t* data = stbi_load(path, &w_, &h_, &ch, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!data) { fprintf(stderr, "[GpuTexture] load failed: %s\n", path); return false; }
    bool ok = InitFromMemory(data, w_, h_, s);
    stbi_image_free(data);
    return ok;
}

bool GpuTexture::InitFromMemory(const uint8_t* rgba8, int w, int h, const GpuSamplerDesc& s) {
    w_ = w; h_ = h;
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        uint32_t num_levels = s.gen_mipmap ? MipLevels(w, h) : 1u;
        SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (s.gen_mipmap) usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;

        SDL_GPUTextureCreateInfo ti = {};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage                = usage;
        ti.width                = (Uint32)w;
        ti.height               = (Uint32)h;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = num_levels;
        sdl_tex_ = SDL_CreateGPUTexture(dev, &ti);
        if (!sdl_tex_) {
            MD_LOG(MD_LOG_WARNING, "[GpuTexture] SDL_CreateGPUTexture failed: %s", SDL_GetError());
            return false;
        }

        uint32_t upload_size = (uint32_t)(w * h * 4);
        SDL_GPUTransferBufferCreateInfo tbuf = {};
        tbuf.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbuf.size  = upload_size;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(dev, &tbuf);
        void* map = SDL_MapGPUTransferBuffer(dev, transfer, false);
        if (map) { memcpy(map, rgba8, upload_size); SDL_UnmapGPUTransferBuffer(dev, transfer); }

        SDL_GPUCommandBuffer* cmd  = SDL_AcquireGPUCommandBuffer(dev);
        SDL_GPUCopyPass*      pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src_info = {};
        src_info.transfer_buffer = transfer;
        src_info.pixels_per_row  = (Uint32)w;
        src_info.rows_per_layer  = (Uint32)h;
        SDL_GPUTextureRegion dst_region = {};
        dst_region.texture = sdl_tex_;
        dst_region.w       = (Uint32)w;
        dst_region.h       = (Uint32)h;
        dst_region.d       = 1;
        SDL_UploadToGPUTexture(pass, &src_info, &dst_region, false);
        SDL_EndGPUCopyPass(pass);
        if (s.gen_mipmap) SDL_GenerateMipmapsForGPUTexture(cmd, sdl_tex_);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(dev, transfer);

        sdl_sampler_ = CreateSDLSampler(dev, s);
        return true;
    }
    // dev == null: SDL_GPU not initialised — fall through to OpenGL path below.
#endif
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
    ApplySampler(s);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void GpuTexture::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_sampler_) { SDL_ReleaseGPUSampler(dev, sdl_sampler_); sdl_sampler_ = nullptr; }
    if (sdl_tex_)     { SDL_ReleaseGPUTexture(dev, sdl_tex_);     sdl_tex_     = nullptr; }
#endif
    if (id_) { glDeleteTextures(1, &id_); id_ = 0; }
    w_ = h_ = 0;
}

void GpuTexture::Bind(uint32_t unit) const {
    if (id_) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, id_);
    }
}

#endif // MD_OPENGL43_ENABLED || MD_SDL_GPU (GpuDepthTexture+GpuTexture+GpuStaticBuffer+GpuRenderPass)

#endif // MD_OPENGL43_ENABLED || MD_SDL_GPU (outer file guard)
