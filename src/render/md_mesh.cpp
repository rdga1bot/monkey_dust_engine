#include <monkey_dust/render/md_mesh.h>

#ifdef MD_OPENGL43_ENABLED
#include <monkey_dust/render/gpu_hal.h>
#include "glad.h"
#include <cmath>
#include <cstdlib>

// ── BuildMesh ────────────────────────────────────────────────────────────────
// Uploads static mesh data using GpuStaticBuffer and extracts GL IDs into
// the legacy MdMesh struct via Release().  VAO still needed for MdDrawMesh.
// SDL_GPU future: remove VAO; store GpuStaticBuffer objects directly.

static MdMesh BuildMesh(const float* pos,  int np,
                        const float* norm, int nn,
                        const float* uv,   int nu,
                        const unsigned int* idx, int ni)
{
    GpuStaticBuffer b_pos, b_norm, b_uv, b_idx;
    b_pos .Init(GL_ARRAY_BUFFER,         pos,  (uint32_t)(np * sizeof(float)));
    b_norm.Init(GL_ARRAY_BUFFER,         norm, (uint32_t)(nn * sizeof(float)));
    b_uv  .Init(GL_ARRAY_BUFFER,         uv,   (uint32_t)(nu * sizeof(float)));
    b_idx .Init(GL_ELEMENT_ARRAY_BUFFER, idx,  (uint32_t)(ni * sizeof(unsigned int)));

    MdMesh m;
    m.index_count = ni;

    // Build VAO: bind each buffer, declare attrib format, then release ownership.
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);

    b_pos.Bind(GL_ARRAY_BUFFER);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    m.vbo_pos = b_pos.Release();

    b_norm.Bind(GL_ARRAY_BUFFER);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);
    m.vbo_norm = b_norm.Release();

    b_uv.Bind(GL_ARRAY_BUFFER);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(2);
    m.vbo_uv = b_uv.Release();

    b_idx.Bind(GL_ELEMENT_ARRAY_BUFFER);
    m.ebo = b_idx.Release();

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
            pos [vi*3+0] = x * radius; pos [vi*3+1] = y * radius; pos [vi*3+2] = z * radius;
            norm[vi*3+0] = x;          norm[vi*3+1] = y;          norm[vi*3+2] = z;
            uv  [ui*2+0] = (float)j / (float)slices;
            uv  [ui*2+1] = (float)i / (float)rings;
            ++vi; ++ui;
        }
    }

    int ii = 0;
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < slices; ++j) {
            unsigned int a = (unsigned int)(i       * (slices+1) + j);
            unsigned int b = (unsigned int)((i+1)   * (slices+1) + j);
            unsigned int c = (unsigned int)((i+1)   * (slices+1) + (j+1));
            unsigned int d = (unsigned int)(i       * (slices+1) + (j+1));
            idx[ii++]=a; idx[ii++]=b; idx[ii++]=c;
            idx[ii++]=a; idx[ii++]=c; idx[ii++]=d;
        }

    MdMesh m = BuildMesh(pos, vcount*3, norm, vcount*3, uv, vcount*2, idx, icount);
    free(pos); free(norm); free(uv); free(idx);
    return m;
}

// ── MdMeshCube ───────────────────────────────────────────────────────────────

MdMesh MdMeshCube(float w, float h, float d) {
    float hx = w*0.5f, hy = h*0.5f, hz = d*0.5f;
    float pos[24*3] = {
         hx,-hy,-hz,  hx, hy,-hz,  hx, hy, hz,  hx,-hy, hz,
        -hx,-hy, hz, -hx, hy, hz, -hx, hy,-hz, -hx,-hy,-hz,
        -hx, hy,-hz, -hx, hy, hz,  hx, hy, hz,  hx, hy,-hz,
        -hx,-hy, hz, -hx,-hy,-hz,  hx,-hy,-hz,  hx,-hy, hz,
        -hx,-hy, hz,  hx,-hy, hz,  hx, hy, hz, -hx, hy, hz,
         hx,-hy,-hz, -hx,-hy,-hz, -hx, hy,-hz,  hx, hy,-hz,
    };
    float norm[24*3] = {
         1,0,0,  1,0,0,  1,0,0,  1,0,0,
        -1,0,0, -1,0,0, -1,0,0, -1,0,0,
         0,1,0,  0,1,0,  0,1,0,  0,1,0,
         0,-1,0, 0,-1,0, 0,-1,0, 0,-1,0,
         0,0,1,  0,0,1,  0,0,1,  0,0,1,
         0,0,-1, 0,0,-1, 0,0,-1, 0,0,-1,
    };
    float uv[24*2] = {
        0,0,1,0,1,1,0,1, 0,0,1,0,1,1,0,1,
        0,0,1,0,1,1,0,1, 0,0,1,0,1,1,0,1,
        0,0,1,0,1,1,0,1, 0,0,1,0,1,1,0,1,
    };
    unsigned int idx[36];
    for (int f = 0; f < 6; ++f) {
        unsigned int base = (unsigned int)(f * 4);
        idx[f*6+0]=base+0; idx[f*6+1]=base+1; idx[f*6+2]=base+2;
        idx[f*6+3]=base+0; idx[f*6+4]=base+2; idx[f*6+5]=base+3;
    }
    return BuildMesh(pos, 24*3, norm, 24*3, uv, 24*2, idx, 36);
}

// ── MdMeshCone ───────────────────────────────────────────────────────────────

MdMesh MdMeshCone(float radius, float height, int slices) {
    int vcount = slices + 2;
    int icount = slices * 6;
    float* pos  = (float*)malloc((size_t)vcount * 3 * sizeof(float));
    float* norm = (float*)malloc((size_t)vcount * 3 * sizeof(float));
    float* uv   = (float*)malloc((size_t)vcount * 2 * sizeof(float));
    unsigned int* idx = (unsigned int*)malloc((size_t)icount * sizeof(unsigned int));
    static constexpr float PI = 3.14159265358979f;
    float hy = height * 0.5f;
    pos[0]=0; pos[1]=hy; pos[2]=0; norm[0]=0; norm[1]=1; norm[2]=0; uv[0]=0.5f; uv[1]=1.f;
    for (int i = 0; i < slices; ++i) {
        float a = 2.f*PI*(float)i/(float)slices;
        float cx = cosf(a), cz = sinf(a);
        int v = i+1;
        pos[v*3+0]=cx*radius; pos[v*3+1]=-hy; pos[v*3+2]=cz*radius;
        norm[v*3+0]=cx; norm[v*3+1]=0; norm[v*3+2]=cz;
        uv[v*2+0]=(float)i/(float)slices; uv[v*2+1]=0.f;
    }
    int vc = slices+1;
    pos[vc*3+0]=0; pos[vc*3+1]=-hy; pos[vc*3+2]=0;
    norm[vc*3+0]=0; norm[vc*3+1]=-1; norm[vc*3+2]=0;
    uv[vc*2+0]=0.5f; uv[vc*2+1]=0.5f;
    int ii = 0;
    for (int i = 0; i < slices; ++i) {
        int next = (i+1) % slices;
        idx[ii++]=0; idx[ii++]=(unsigned int)(i+1); idx[ii++]=(unsigned int)(next+1);
        idx[ii++]=(unsigned int)vc; idx[ii++]=(unsigned int)(next+1); idx[ii++]=(unsigned int)(i+1);
    }
    MdMesh m = BuildMesh(pos, vcount*3, norm, vcount*3, uv, vcount*2, idx, icount);
    free(pos); free(norm); free(uv); free(idx);
    return m;
}

// ── MdMeshPlane ──────────────────────────────────────────────────────────────

MdMesh MdMeshPlane(float w, float d) {
    float hw = w*0.5f, hd = d*0.5f;
    float pos[4*3]  = { -hw,0,-hd,  hw,0,-hd,  hw,0, hd, -hw,0, hd };
    float norm[4*3] = {  0,1,0,     0,1,0,      0,1,0,    0,1,0     };
    float uv[4*2]   = {  0,0,  1,0,  1,1,  0,1 };
    unsigned int idx[6] = { 0,1,2, 0,2,3 };
    return BuildMesh(pos, 4*3, norm, 4*3, uv, 4*2, idx, 6);
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
    glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

#endif
