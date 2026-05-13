#pragma once
// MdMesh — VAO/VBO/EBO mesh wrapper using direct glad (no Raylib).
// Under !MD_OPENGL43_ENABLED all functions are no-ops / return {}.
//
// Vertex layout (fixed):
//   attrib 0 — vec3 position
//   attrib 1 — vec3 normal
//   attrib 2 — vec2 uv

struct MdMesh {
    unsigned int vao       = 0;
    unsigned int vbo_pos   = 0;
    unsigned int vbo_norm  = 0;
    unsigned int vbo_uv    = 0;
    unsigned int ebo       = 0;
    int          index_count = 0;
};

inline MdMesh MdMeshSphere(float, int, int)    { return {}; }
inline MdMesh MdMeshCube  (float, float, float) { return {}; }
inline MdMesh MdMeshCone  (float, float, int)  { return {}; }
inline MdMesh MdMeshPlane (float, float)        { return {}; }
inline MdMesh MdMeshFromBuffers(const float*, const float*, const float*,
                                const unsigned int*, int, int) { return {}; }
inline void   MdUnloadMesh(MdMesh&) {}
inline void   MdDrawMesh  (MdMesh)  {}
