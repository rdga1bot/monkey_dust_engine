#include <monkey_dust/render/md_mesh.h>

#ifdef MD_OPENGL43_ENABLED
#include "external/glad.h"
#include <cmath>
#include <cstdlib>

// ── helpers ─────────────────────────────────────────────────────────────────

static MdMesh BuildMesh(const float* pos,  int np,
                        const float* norm, int nn,
                        const float* uv,   int nu,
                        const unsigned int* idx, int ni)
{
    MdMesh m;
    m.index_count = ni;

    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);

    glGenBuffers(1, &m.vbo_pos);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, np * sizeof(float), pos, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    glGenBuffers(1, &m.vbo_norm);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo_norm);
    glBufferData(GL_ARRAY_BUFFER, nn * sizeof(float), norm, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(1);

    glGenBuffers(1, &m.vbo_uv);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo_uv);
    glBufferData(GL_ARRAY_BUFFER, nu * sizeof(float), uv, GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(2);

    glGenBuffers(1, &m.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ni * sizeof(unsigned int), idx, GL_STATIC_DRAW);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return m;
}

// ── MdMeshFromBuffers ────────────────────────────────────────────────────────

MdMesh MdMeshFromBuffers(const float* pos, const float* norm, const float* uv,
                         const unsigned int* idx, int vcount, int icount)
{
    return BuildMesh(pos, vcount*3, norm, vcount*3, uv, vcount*2, idx, icount);
}

// ── MdMeshSphere ─────────────────────────────────────────────────────────────

MdMesh MdMeshSphere(float radius, int rings, int slices) {
    int vcount = (rings + 1) * (slices + 1);
    int icount = rings * slices * 6;

    float* pos  = (float*)malloc((size_t)vcount * 3 * sizeof(float));
    float* norm = (float*)malloc((size_t)vcount * 3 * sizeof(float));
    float* uv   = (float*)malloc((size_t)vcount * 2 * sizeof(float));
    unsigned int* idx = (unsigned int*)malloc((size_t)icount * sizeof(unsigned int));

    static constexpr float PI = 3.14159265358979f;
    int vi = 0, ui = 0;
    for (int i = 0; i <= rings; ++i) {
        float phi = PI * (float)i / (float)rings;
        float sp  = sinf(phi), cp = cosf(phi);
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.f * PI * (float)j / (float)slices;
            float st = sinf(theta), ct = cosf(theta);
            float x = sp * ct, y = cp, z = sp * st;
            pos [vi*3+0] = x * radius;
            pos [vi*3+1] = y * radius;
            pos [vi*3+2] = z * radius;
            norm[vi*3+0] = x;
            norm[vi*3+1] = y;
            norm[vi*3+2] = z;
            uv  [ui*2+0] = (float)j / (float)slices;
            uv  [ui*2+1] = (float)i / (float)rings;
            ++vi; ++ui;
        }
    }

    int ii = 0;
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < slices; ++j) {
            unsigned int a = (unsigned int)(i       * (slices + 1) + j);
            unsigned int b = (unsigned int)((i + 1) * (slices + 1) + j);
            unsigned int c = (unsigned int)((i + 1) * (slices + 1) + (j + 1));
            unsigned int d = (unsigned int)(i       * (slices + 1) + (j + 1));
            idx[ii++] = a; idx[ii++] = b; idx[ii++] = c;
            idx[ii++] = a; idx[ii++] = c; idx[ii++] = d;
        }
    }

    MdMesh m = BuildMesh(pos, vcount*3, norm, vcount*3, uv, vcount*2, idx, icount);
    free(pos); free(norm); free(uv); free(idx);
    return m;
}

// ── MdMeshCube ───────────────────────────────────────────────────────────────

MdMesh MdMeshCube(float w, float h, float d) {
    float hx = w * 0.5f, hy = h * 0.5f, hz = d * 0.5f;

    // 6 faces × 4 verts = 24 vertices
    float pos[24*3] = {
        // +X
         hx,-hy,-hz,  hx, hy,-hz,  hx, hy, hz,  hx,-hy, hz,
        // -X
        -hx,-hy, hz, -hx, hy, hz, -hx, hy,-hz, -hx,-hy,-hz,
        // +Y
        -hx, hy,-hz, -hx, hy, hz,  hx, hy, hz,  hx, hy,-hz,
        // -Y
        -hx,-hy, hz, -hx,-hy,-hz,  hx,-hy,-hz,  hx,-hy, hz,
        // +Z
        -hx,-hy, hz,  hx,-hy, hz,  hx, hy, hz, -hx, hy, hz,
        // -Z
         hx,-hy,-hz, -hx,-hy,-hz, -hx, hy,-hz,  hx, hy,-hz,
    };
    float norm[24*3] = {
         1,0,0, 1,0,0, 1,0,0, 1,0,0,
        -1,0,0,-1,0,0,-1,0,0,-1,0,0,
         0,1,0, 0,1,0, 0,1,0, 0,1,0,
         0,-1,0,0,-1,0,0,-1,0,0,-1,0,
         0,0,1, 0,0,1, 0,0,1, 0,0,1,
         0,0,-1,0,0,-1,0,0,-1,0,0,-1,
    };
    float uv[24*2] = {
        0,0,1,0,1,1,0,1,
        0,0,1,0,1,1,0,1,
        0,0,1,0,1,1,0,1,
        0,0,1,0,1,1,0,1,
        0,0,1,0,1,1,0,1,
        0,0,1,0,1,1,0,1,
    };
    // 6 faces × 6 indices = 36
    unsigned int idx[36];
    for (int f = 0; f < 6; ++f) {
        unsigned int base = (unsigned int)(f * 4);
        idx[f*6+0] = base+0; idx[f*6+1] = base+1; idx[f*6+2] = base+2;
        idx[f*6+3] = base+0; idx[f*6+4] = base+2; idx[f*6+5] = base+3;
    }
    return BuildMesh(pos, 24*3, norm, 24*3, uv, 24*2, idx, 36);
}

// ── utilities ────────────────────────────────────────────────────────────────

void MdUnloadMesh(MdMesh& m) {
    if (m.vao) {
        glDeleteVertexArrays(1, &m.vao);
        glDeleteBuffers(1, &m.vbo_pos);
        glDeleteBuffers(1, &m.vbo_norm);
        glDeleteBuffers(1, &m.vbo_uv);
        glDeleteBuffers(1, &m.ebo);
        m = {};
    }
}

void MdDrawMesh(MdMesh m) {
    if (!m.vao) return;
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
#endif
