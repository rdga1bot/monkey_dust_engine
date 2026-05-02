#include <monkey_dust/render/gpu_hal.h>

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#include <monkey_dust/platform/md_log.h>

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

#endif // MD_OPENGL43_ENABLED
