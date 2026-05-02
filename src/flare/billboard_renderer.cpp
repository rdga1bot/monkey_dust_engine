#include <monkey_dust/flare/billboard_renderer.h>
#include <monkey_dust/render/md_shader.h>

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#include <cstring>
#include <cstdio>

// GPU instance layout (interleaved, matches billboard.vert locations 1-4):
//   vec3 world_pos    offset 0
//   vec2 size         offset 12
//   vec4 uv_rect      offset 20
//   vec4 tint (byte)  offset 36
//   stride = 40
// atlas_idx is CPU-only (used for sorting); not sent to GPU.
static constexpr int INST_STRIDE   = 40;
static constexpr int INST_OFF_POS  = 0;
static constexpr int INST_OFF_SIZE = 12;
static constexpr int INST_OFF_UV   = 20;
static constexpr int INST_OFF_TINT = 36;

static const float QUAD_VERTS[8] = {
    -1.f, -1.f,   1.f, -1.f,   1.f,  1.f,  -1.f,  1.f
};

namespace md::flare {

BillboardRenderer& BillboardRenderer::Get() {
    static BillboardRenderer inst;
    return inst;
}

// Pack BillboardInstance array → flat GPU buffer (excludes atlas_idx).
static int PackInstances(const BillboardInstance* src, int count,
                         uint8_t* dst, int dst_capacity_bytes)
{
    int max = dst_capacity_bytes / INST_STRIDE;
    if (count > max) count = max;
    for (int i = 0; i < count; ++i) {
        uint8_t* p = dst + (size_t)i * INST_STRIDE;
        const BillboardInstance& s = src[i];
        memcpy(p + INST_OFF_POS,  &s.x,    12);
        memcpy(p + INST_OFF_SIZE, &s.width,  8);
        memcpy(p + INST_OFF_UV,   &s.u0,   16);
        p[INST_OFF_TINT + 0] = s.r;
        p[INST_OFF_TINT + 1] = s.g;
        p[INST_OFF_TINT + 2] = s.b;
        p[INST_OFF_TINT + 3] = s.a;
    }
    return count;
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────

void BillboardRenderer::Init() {
    if (init_) return;

    glGenBuffers(1, &quad_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTS), QUAD_VERTS, GL_STATIC_DRAW);

    glGenBuffers(1, &inst_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferData(GL_ARRAY_BUFFER, MAX_BILLBOARDS * INST_STRIDE, nullptr, GL_STREAM_DRAW);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glVertexAttribDivisor(0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, INST_STRIDE, (void*)INST_OFF_POS);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, INST_STRIDE, (void*)INST_OFF_SIZE);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, INST_STRIDE, (void*)INST_OFF_UV);
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, INST_STRIDE, (void*)INST_OFF_TINT);
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);

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
    UnloadAllAtlases();
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

    // Counting sort by atlas_idx — O(N), max 4 passes.
    static BillboardInstance sorted[MAX_BILLBOARDS];
    int atlas_start[MAX_ATLAS + 1] = {};
    for (int i = 0; i < count_; ++i) {
        int ai = instances_[i].atlas_idx < MAX_ATLAS ? instances_[i].atlas_idx : 0;
        atlas_start[ai + 1]++;
    }
    for (int i = 1; i <= MAX_ATLAS; ++i)
        atlas_start[i] += atlas_start[i - 1];
    {
        int cursor[MAX_ATLAS] = {};
        for (int i = 0; i < count_; ++i) {
            int ai = instances_[i].atlas_idx < MAX_ATLAS ? instances_[i].atlas_idx : 0;
            sorted[atlas_start[ai] + cursor[ai]++] = instances_[i];
        }
    }

    // Compute view/proj matrices (portable via mat4_ptr).
#ifdef USE_GLM
    Mat4 view_m4 = cam.ViewMatrix();
    Mat4 proj_m4 = cam.ProjMatrix(aspect);
    const float* vf = mat4_ptr(view_m4);
    const float* pf = mat4_ptr(proj_m4);
#else
    Mat4 view_m4 = cam.ViewMatrix();
    Mat4 proj_m4 = cam.ProjMatrix(aspect);
    const float* vf = mat4_ptr(view_m4);
    const float* pf = mat4_ptr(proj_m4);
#endif
    // Camera basis from view matrix (column-major: col0=right, col1=up).
    float right[3] = { vf[0], vf[4], vf[8] };
    float up[3]    = { vf[1], vf[5], vf[9] };

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(prog_);
    glUniformMatrix4fv(loc_view_,      1, GL_FALSE, vf);
    glUniformMatrix4fv(loc_proj_,      1, GL_FALSE, pf);
    glUniform3fv(loc_cam_right_, 1, right);
    glUniform3fv(loc_cam_up_,    1, up);
    glUniform1f (loc_alpha_thr_, 0.5f);

    static uint8_t gpu_buf[MAX_BILLBOARDS * INST_STRIDE];
    glBindVertexArray(vao_);

    for (int ai = 0; ai < MAX_ATLAS; ++ai) {
        int start = atlas_start[ai];
        int n     = atlas_start[ai + 1] - start;
        if (n == 0 || !atlases_[ai].id) continue;

        int packed = PackInstances(sorted + start, n, gpu_buf, (int)sizeof(gpu_buf));
        glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(packed * INST_STRIDE), gpu_buf);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        MdBindTexture(atlases_[ai], 0);
        glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, packed);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glDepthMask(GL_TRUE);
}

// ── Atlas management ──────────────────────────────────────────────────────────

void BillboardRenderer::LoadSpriteAtlas(const char* png_path, int idx) {
    if (idx < 0 || idx >= MAX_ATLAS) return;
    MdUnloadTexture(atlases_[idx]);
    atlases_[idx] = MdLoadTexturePixelArt(png_path);
    if (!atlases_[idx].id)
        fprintf(stderr, "[Billboard] atlas[%d] failed: %s\n", idx, png_path);
}

void BillboardRenderer::UnloadAllAtlases() {
    for (int i = 0; i < MAX_ATLAS; ++i) MdUnloadTexture(atlases_[i]);
}

} // namespace md::flare
#endif // MD_OPENGL43_ENABLED
