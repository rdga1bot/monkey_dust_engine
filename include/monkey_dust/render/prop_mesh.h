#pragma once
// PropMesh — loads a GLB file (first mesh primitive) into GPU static buffers.
// Vertex layout: PropVertex { float x,y,z, nx,ny,nz; } — stride=24 bytes.
// Index format: uint16_t if vertex count <= 65535, else uint32_t.
// Usage: Init once; Draw* calls reference vbo/ibo; Shutdown on exit.
#include <monkey_dust/render/gpu_hal.h>
#include <cstdint>

struct PropVertex {
    float x, y, z;    // position
    float nx, ny, nz; // normal
};
static_assert(sizeof(PropVertex) == 24, "PropVertex stride mismatch");

class PropMesh {
public:
    // Load GLB from path.  Returns false and sets loaded=false on failure.
    // Passing nullptr as path is a no-op — returns false immediately.
    bool LoadGLB(const char* path);
    void Shutdown();

    GpuStaticBuffer vbo;
    GpuStaticBuffer ibo;
    uint32_t        index_count = 0;
    bool            loaded      = false;
    bool            indices_u16 = true;
    float           aabb_y_min  = 0.0f;  // model-space Y extent (for animation)
    float           aabb_y_max  = 1.0f;
};
