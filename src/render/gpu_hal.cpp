#include <monkey_dust/render/gpu_hal.h>

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#include <monkey_dust/platform/md_log.h>
#include <cstdlib>
#include <cstdio>

// ── Helpers ───────────────────────────────────────────────────────────────────

static void ApplyAttribFormat(const GpuVertexAttrib& a) {
    // GL 4.3 separate attrib format: format is in the VAO, buffer binding is separate.
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
    glVertexAttribBinding(a.location, 0); // all attribs use vertex binding slot 0
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

// ── GpuPipeline ───────────────────────────────────────────────────────────────

bool GpuPipeline::Create(const Desc& desc) {
    if (!desc.vert_path || !desc.frag_path) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] Create: null shader paths");
        return false;
    }
    shader_ = MdLoadShader(desc.vert_path, desc.frag_path);
    if (!shader_.id) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] MdLoadShader failed: %s", desc.vert_path);
        return false;
    }
    raster_ = desc.raster;

    // Build VAO using GL 4.3 separate attrib format.
    // Vertex layout (format + binding) lives in VAO; buffer data bound separately.
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    for (uint32_t i = 0; i < desc.layout.count; ++i)
        ApplyAttribFormat(desc.layout.attribs[i]);
    glBindVertexArray(0);

    return true;
}

void GpuPipeline::Destroy() {
    MdUnloadShader(shader_);
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

int GpuPipeline::UniformLoc(const char* name) const {
    return MdGetLoc(shader_, name);
}

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

    MdUseShader(p->shader_);

    const GpuRasterState& r = p->raster_;
    if (r.blend_enable) {
        glEnable(GL_BLEND);
        glBlendFunc(r.src_factor, r.dst_factor);
    }
    if (!r.depth_test)  glDisable(GL_DEPTH_TEST);
    if (!r.depth_write) glDepthMask(GL_FALSE);
    if (!r.cull_back)   glDisable(GL_CULL_FACE);
    if (r.point_size)   glEnable(GL_PROGRAM_POINT_SIZE);
}

void GpuCommandBuffer::BindVertexBuffer(GpuVertexBuffer* buf) {
    if (!pipeline_ || !buf) return;
    // Bind the VAO (captures attrib layout); connect current ring slot via DSA.
    glBindVertexArray(pipeline_->vao_);
    glBindVertexBuffer(0,                     // binding slot 0
                       buf->ring_.GLBuffer(),  // ring's GL buffer object
                       buf->ring_.GLOffset(),  // offset to current frame's data
                       (GLsizei)buf->stride_); // bytes per vertex
}

void GpuCommandBuffer::SetUniformMat4(int loc, const float* m16) {
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m16);
}

void GpuCommandBuffer::SetUniformVec3(int loc, const float* v3) {
    if (loc >= 0) glUniform3fv(loc, 1, v3);
}

void GpuCommandBuffer::Draw(uint32_t vertex_count, uint32_t first_vertex) {
    if (!pipeline_) return;
    glDrawArrays(ToGL(pipeline_->raster_.topology),
                 (GLint)first_vertex, (GLsizei)vertex_count);
}

void GpuCommandBuffer::EndPass() {
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
}

// ── GpuComputePipeline ────────────────────────────────────────────────────────

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
        // Clamp-to-border with white (depth=1) ensures regions outside shadow map
        // are treated as fully lit (PCF kernel reads off-map → 1.0 depth → no shadow).
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

// stb_image declarations (implementation from libraylib.a or stb_image_impl.cpp)
#include "stb_image.h"
#include <cstdio>

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
