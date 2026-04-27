#include <monkey_dust/flare/billboard_renderer.h>
#include <monkey_dust/render/md_shader.h>

#ifdef MD_OPENGL43_ENABLED
#include "external/glad.h"
#include "raylib.h"   // Matrix, Vector3
#include <cstring>
#include <cstdio>

// GPU instance layout (interleaved, matches billboard.vert locations 1-4):
//   vec3 world_pos    offset 0
//   vec2 size         offset 12
//   vec4 uv_rect      offset 20
//   vec4 tint (byte)  offset 36
//   stride = 40
static constexpr int INST_STRIDE   = 40;
static constexpr int INST_OFF_POS  = 0;
static constexpr int INST_OFF_SIZE = 12;
static constexpr int INST_OFF_UV   = 20;
static constexpr int INST_OFF_TINT = 36;

// Quad corners (shared): 4 vertices forming a unit square.
static const float QUAD_VERTS[8] = {
    -1.f, -1.f,   1.f, -1.f,   1.f,  1.f,  -1.f,  1.f
};

namespace md::flare {

BillboardRenderer& BillboardRenderer::Get() {
    static BillboardRenderer inst;
    return inst;
}

// ── GPU upload helper ─────────────────────────────────────────────────────────

// Pack BillboardInstance array → flat GPU buffer.
static int PackInstances(const BillboardInstance* src, int count,
                         uint8_t* dst, int dst_capacity_bytes)
{
    int max = dst_capacity_bytes / INST_STRIDE;
    if (count > max) count = max;
    for (int i = 0; i < count; ++i) {
        uint8_t* p = dst + (size_t)i * INST_STRIDE;
        const BillboardInstance& s = src[i];
        memcpy(p + INST_OFF_POS,  &s.x,  12);   // xyz float
        memcpy(p + INST_OFF_SIZE, &s.width, 8);  // wh float
        memcpy(p + INST_OFF_UV,   &s.u0,   16);  // uv rect float
        p[INST_OFF_TINT + 0] = s.r;
        p[INST_OFF_TINT + 1] = s.g;
        p[INST_OFF_TINT + 2] = s.b;
        p[INST_OFF_TINT + 3] = s.a;
    }
    return count;
}

// ── Shader compile ────────────────────────────────────────────────────────────

static GLuint CompileShader(const char* vert_src, const char* frag_src) {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(sh, 512, nullptr, log);
            fprintf(stderr, "[Billboard] shader error: %s\n", log);
            glDeleteShader(sh); return 0;
        }
        return sh;
    };
    GLuint vs = compile(GL_VERTEX_SHADER,   vert_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(prog, 512, nullptr, log);
        fprintf(stderr, "[Billboard] link error: %s\n", log);
        glDeleteProgram(prog); return 0;
    }
    return prog;
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────

void BillboardRenderer::Init() {
    if (init_) return;

    // Quad VBO (4 corners).
    glGenBuffers(1, &quad_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTS), QUAD_VERTS, GL_STATIC_DRAW);

    // Instance VBO (stream).
    glGenBuffers(1, &inst_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferData(GL_ARRAY_BUFFER, MAX_BILLBOARDS * INST_STRIDE, nullptr, GL_STREAM_DRAW);

    // VAO layout.
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    // Attrib 0: quad corner (vec2, from quad_vbo_)
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glVertexAttribDivisor(0, 0);   // per-vertex

    // Attribs 1-4: per-instance (from inst_vbo_)
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, INST_STRIDE,
                          (void*)INST_OFF_POS);
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, INST_STRIDE,
                          (void*)INST_OFF_SIZE);
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, INST_STRIDE,
                          (void*)INST_OFF_UV);
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, INST_STRIDE,
                          (void*)INST_OFF_TINT);
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);

    // Shader (loaded from files).
    prog_ = MdLoadShader("shaders/billboard.vert", "shaders/billboard.frag").id;
    if (!prog_) {
        fprintf(stderr, "[Billboard] failed to load shaders\n");
    } else {
        loc_view_      = glGetUniformLocation(prog_, "u_view");
        loc_proj_      = glGetUniformLocation(prog_, "u_proj");
        loc_cam_right_ = glGetUniformLocation(prog_, "u_camera_right");
        loc_cam_up_    = glGetUniformLocation(prog_, "u_camera_up");
        loc_alpha_thr_ = glGetUniformLocation(prog_, "u_alpha_threshold");
    }

    init_ = true;
}

void BillboardRenderer::Shutdown() {
    if (!init_) return;
    MdUnloadTexture(atlas_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &quad_vbo_);
    glDeleteBuffers(1, &inst_vbo_);
    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
    init_ = false;
}

// ── Per-frame ─────────────────────────────────────────────────────────────────

void BillboardRenderer::BeginFrame() { count_ = 0; }

void BillboardRenderer::Submit(const BillboardInstance& inst) {
    if (count_ < MAX_BILLBOARDS) instances_[count_++] = inst;
}

int BillboardRenderer::SubmittedCount() const { return count_; }

void BillboardRenderer::Render(const MdCamera& cam, float aspect) {
    if (!init_ || !prog_ || count_ == 0) return;

    // Build packed GPU buffer on stack (40 bytes × 4096 = 160 KB — fits).
    static uint8_t gpu_buf[MAX_BILLBOARDS * INST_STRIDE];
    int n = PackInstances(instances_, count_, gpu_buf, (int)sizeof(gpu_buf));

    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * INST_STRIDE), gpu_buf);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Compute view/proj as Raylib Matrix (works for both USE_GLM and !USE_GLM).
#ifdef USE_GLM
    Matrix view = GlmToRaylibMat(cam.ViewMatrix());
    Matrix proj = GlmToRaylibMat(cam.ProjMatrix(aspect));
#else
    Matrix view = cam.ViewMatrix();
    Matrix proj = cam.ProjMatrix(aspect);
#endif

    // Camera basis vectors from view matrix columns.
    float right[3] = { view.m0, view.m4, view.m8 };
    float up[3]    = { view.m1, view.m5, view.m9 };

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);   // don't write depth for transparent sprites
    glUseProgram(prog_);

    glUniformMatrix4fv(loc_view_, 1, GL_FALSE, &view.m0);
    glUniformMatrix4fv(loc_proj_, 1, GL_FALSE, &proj.m0);
    glUniform3fv(loc_cam_right_, 1, right);
    glUniform3fv(loc_cam_up_,    1, up);
    glUniform1f (loc_alpha_thr_, 0.5f);

    if (atlas_.id) MdBindTexture(atlas_, 0);

    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, n);
    glBindVertexArray(0);

    glUseProgram(0);
    glDepthMask(GL_TRUE);
}

// ── Atlas ─────────────────────────────────────────────────────────────────────

void BillboardRenderer::LoadSpriteAtlas(const char* png_path) {
    MdUnloadTexture(atlas_);
    atlas_ = MdLoadTexture(png_path);
    if (!atlas_.id)
        fprintf(stderr, "[Billboard] failed to load atlas: %s\n", png_path);
}

void BillboardRenderer::UnloadSpriteAtlas() {
    MdUnloadTexture(atlas_);
}

} // namespace md::flare
#endif // MD_OPENGL43_ENABLED
