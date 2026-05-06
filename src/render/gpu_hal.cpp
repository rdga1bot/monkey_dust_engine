#include <monkey_dust/render/gpu_hal.h>

#if defined(MD_OPENGL43_ENABLED) || defined(MD_SDL_GPU)
#include <monkey_dust/platform/md_log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#endif

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
    SDL_GPUVertexBufferDescription vbd = {};
    vbd.slot             = 0;
    vbd.pitch            = desc.layout.stride;
    vbd.input_rate       = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbd.instance_step_rate = 0;

    SDL_GPUVertexAttribute vattribs[8] = {};
    for (uint32_t i = 0; i < desc.layout.count; ++i) {
        vattribs[i].location    = desc.layout.attribs[i].location;
        vattribs[i].buffer_slot = 0;
        vattribs[i].format      = ToSDLFmt(desc.layout.attribs[i].fmt);
        vattribs[i].offset      = desc.layout.attribs[i].offset;
    }

    SDL_GPUVertexInputState vertex_input = {};
    vertex_input.vertex_buffer_descriptions = (desc.layout.stride > 0) ? &vbd : nullptr;
    vertex_input.num_vertex_buffers         = (desc.layout.stride > 0) ? 1u  : 0u;
    vertex_input.vertex_attributes          = (desc.layout.count > 0)  ? vattribs : nullptr;
    vertex_input.num_vertex_attributes      = desc.layout.count;

    // Color target (uses swapchain format)
    SDL_Window* win = md::GpuDevice::Get().Window();
    SDL_GPUTextureFormat swapchain_fmt = SDL_GetGPUSwapchainTextureFormat(dev, win);

    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = swapchain_fmt;
    if (desc.raster.blend_enable) {
        color_target.blend_state.enable_blend          = SDL_TRUE;
        color_target.blend_state.src_color_blendfactor = ToSDLBlend(desc.raster.src_factor);
        color_target.blend_state.dst_color_blendfactor = ToSDLBlend(desc.raster.dst_factor);
        color_target.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
        color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        color_target.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
        color_target.blend_state.color_write_mask      = 0xF;
    }

    SDL_GPUGraphicsPipelineTargetInfo target_info = {};
    target_info.color_target_descriptions = &color_target;
    target_info.num_color_targets         = 1;
    if (desc.has_depth_target) {
        target_info.depth_stencil_format       = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        target_info.has_depth_stencil_target   = SDL_TRUE;
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
    ci.depth_stencil_state.enable_depth_test  = desc.raster.depth_test  ? SDL_TRUE : SDL_FALSE;
    ci.depth_stencil_state.enable_depth_write = desc.raster.depth_write ? SDL_TRUE : SDL_FALSE;
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

#ifdef MD_OPENGL43_ENABLED
int GpuPipeline::UniformLoc(const char* name) const {
    return MdGetLoc(shader_, name);
}
#endif

// ── GpuVertexBuffer ───────────────────────────────────────────────────────────

void GpuVertexBuffer::Init(uint32_t max_vertices, uint32_t vertex_stride) {
    stride_ = vertex_stride;
    ring_.Init(max_vertices * vertex_stride);
}

void GpuVertexBuffer::Shutdown() { ring_.Shutdown(); stride_ = 0; }

void* GpuVertexBuffer::MapWrite() { return ring_.MapWrite(); }
void  GpuVertexBuffer::Unmap()    { ring_.Unmap(); }
void  GpuVertexBuffer::Advance()  { ring_.Advance(); }

// ── GpuCommandBuffer ─────────────────────────────────────────────────────────

void GpuCommandBuffer::BindPipeline(GpuPipeline* p) {
    pipeline_ = p;
    if (!p) return;

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
#ifdef MD_OPENGL43_ENABLED
    if (!pipeline_) return;
    glDrawArrays(ToGL(pipeline_->raster_.topology),
                 (GLint)first_vertex, (GLsizei)vertex_count);
#else
    (void)vertex_count; (void)first_vertex;
#endif
}

void GpuCommandBuffer::EndPass() {
#ifdef MD_OPENGL43_ENABLED
    if (!pipeline_) return;
    glBindVertexArray(0);
    const GpuRasterState& r = pipeline_->raster_;
    if (r.blend_enable)  glDisable(GL_BLEND);
    if (!r.depth_test)   glEnable(GL_DEPTH_TEST);
    if (!r.depth_write)  glDepthMask(GL_TRUE);
    if (!r.cull_back)    glEnable(GL_CULL_FACE);
    if (r.point_size)    glDisable(GL_PROGRAM_POINT_SIZE);
    MdStopShader();
    pipeline_ = nullptr;
#endif
}

// ── GpuComputePipeline ────────────────────────────────────────────────────────

#ifdef MD_OPENGL43_ENABLED

bool GpuComputePipeline::Create(const char* path) {
    char* src = ReadTextFile(path);
    if (!src) {
        MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] file not found: %s", path);
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
        MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] compile error %s: %s", path, log);
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
        MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] link error %s: %s", path, log);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    return true;
}

void GpuComputePipeline::Destroy() {
    if (program_) { glDeleteProgram(program_); program_ = 0; }
}

int GpuComputePipeline::UniformLoc(const char* name) const {
    return program_ ? (int)glGetUniformLocation(program_, name) : -1;
}

// ── GpuComputePass ────────────────────────────────────────────────────────────

void GpuComputePass::Begin(GpuComputePipeline* pipeline) {
    pipeline_ = pipeline;
    if (pipeline_ && pipeline_->program_) glUseProgram(pipeline_->program_);
}

void GpuComputePass::SetUniformFloat(int loc, float v) {
    if (loc >= 0) glUniform1f(loc, v);
}

void GpuComputePass::SetUniformInt(int loc, int v) {
    if (loc >= 0) glUniform1i(loc, v);
}

void GpuComputePass::SetUniformVec3(int loc, const float* v3) {
    if (loc >= 0) glUniform3fv(loc, 1, v3);
}

void GpuComputePass::SetUniformVec4Array(int loc, const float* v4, int count) {
    if (loc >= 0) glUniform4fv(loc, count, v4);
}

void GpuComputePass::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
    glDispatchCompute(gx, gy, gz);
}

void GpuComputePass::End(uint32_t barrier_flags) {
    glUseProgram(0);
    GLbitfield bits = 0;
    if (barrier_flags & BARRIER_STORAGE) bits |= GL_SHADER_STORAGE_BARRIER_BIT;
    if (barrier_flags & BARRIER_COMMAND)  bits |= GL_COMMAND_BARRIER_BIT;
    if (bits) glMemoryBarrier(bits);
    pipeline_ = nullptr;
}

// ── GpuDepthTexture ───────────────────────────────────────────────────────────

void GpuDepthTexture::Init(int w, int h, bool shadow_border) {
    w_ = w; h_ = h;
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
}

void GpuDepthTexture::Shutdown() {
    if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (tex_) { glDeleteTextures(1,    &tex_); tex_ = 0; }
    w_ = h_ = 0;
}

void GpuDepthTexture::Bind(uint32_t unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex_);
}

// ── GpuRenderPass ─────────────────────────────────────────────────────────────

void GpuRenderPass::BeginDepthOnly(const DepthDesc& desc) {
    cull_front_ = desc.cull_front;
    glGetIntegerv(GL_VIEWPORT, saved_vp_);
    glBindFramebuffer(GL_FRAMEBUFFER, desc.target->FBO());
    glViewport(0, 0, desc.target->Width(), desc.target->Height());
    glClearDepthf(desc.clear_depth);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (cull_front_) glCullFace(GL_FRONT);
}

void GpuRenderPass::End() {
    if (cull_front_) glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(saved_vp_[0], saved_vp_[1], saved_vp_[2], saved_vp_[3]);
    cull_front_ = false;
}

// ── GpuDrawIndexedIndirect ────────────────────────────────────────────────────

void GpuDrawIndexedIndirect(unsigned int indirect_buf_id, uint32_t draw_count) {
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buf_id);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
                                (GLsizei)draw_count, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

// ── GpuStaticBuffer ───────────────────────────────────────────────────────────

void GpuStaticBuffer::Init(unsigned int target, const void* data, uint32_t size) {
    glGenBuffers(1, &gl_buf_);
    glBindBuffer(target, gl_buf_);
    glBufferData(target, (GLsizeiptr)size, data, GL_STATIC_DRAW);
    glBindBuffer(target, 0);
}

void GpuStaticBuffer::Shutdown() {
    if (gl_buf_) { glDeleteBuffers(1, &gl_buf_); gl_buf_ = 0; }
}

void GpuStaticBuffer::Bind(unsigned int target) const {
    glBindBuffer(target, gl_buf_);
}

void GpuStaticBuffer::BindVertex(uint32_t slot, uint32_t stride, uint64_t offset) const {
    glBindVertexBuffer((GLuint)slot, gl_buf_, (GLintptr)offset, (GLsizei)stride);
}

// ── GpuTexture ────────────────────────────────────────────────────────────────

#include "stb_image.h"

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
    if (!data) {
        fprintf(stderr, "[GpuTexture] load failed: %s\n", path);
        return false;
    }
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    ApplySampler(s);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return true;
}

bool GpuTexture::InitFromMemory(const uint8_t* rgba8, int w, int h, const GpuSamplerDesc& s) {
    w_ = w; h_ = h;
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
    ApplySampler(s);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void GpuTexture::Shutdown() {
    if (id_) { glDeleteTextures(1, &id_); id_ = 0; w_ = h_ = 0; }
}

void GpuTexture::Bind(uint32_t unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

#endif // MD_OPENGL43_ENABLED

#endif // MD_OPENGL43_ENABLED || MD_SDL_GPU
