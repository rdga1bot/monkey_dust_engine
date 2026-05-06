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

#ifdef MD_OPENGL43_ENABLED

MdMesh MdMeshSphere(float radius, int rings, int slices);
MdMesh MdMeshCube  (float w, float h, float d);
MdMesh MdMeshCone  (float radius, float height, int slices);
MdMesh MdMeshPlane (float w, float d);

// Build mesh from raw CPU buffers — used by cgltf loader (ModelManager).
// pos/norm: float[vcount*3],  uv: float[vcount*2],  idx: uint[icount]
MdMesh MdMeshFromBuffers(const float* pos, const float* norm, const float* uv,
                         const unsigned int* idx, int vcount, int icount);

void   MdUnloadMesh(MdMesh& m);
void   MdDrawMesh  (MdMesh m);

#else
inline MdMesh MdMeshSphere(float, int, int)    { return {}; }
inline MdMesh MdMeshCube  (float, float, float) { return {}; }
inline MdMesh MdMeshCone  (float, float, int)  { return {}; }
inline MdMesh MdMeshPlane (float, float)        { return {}; }
inline MdMesh MdMeshFromBuffers(const float*, const float*, const float*,
                                const unsigned int*, int, int) { return {}; }
inline void   MdUnloadMesh(MdMesh&) {}
inline void   MdDrawMesh  (MdMesh)  {}
#endif
