#pragma once
// WorldBVH — BVH over world collision geometry for ray picking.
//
// Workflow:
//   1. Call Build() once after world geometry is available.
//   2. Call RayIntersect() per mouse click to find hit world tile.
//
// Built on tinybvh (MIT, Jacco Bikker) — fast BVH construction + traversal.
// Thread-safe for read (RayIntersect), single-threaded for write (Build).

#include <cstdint>

namespace md {

// Result of a ray–world intersection query.
struct WorldRayHit {
    float  t      = 1e30f;  // ray parameter at hit (1e30 = no hit)
    float  world_x = 0.f;  // world-space hit position
    float  world_y = 0.f;
    float  world_z = 0.f;
    int    tri_idx = -1;    // index of hit triangle (-1 = no hit)
    bool   hit()   const { return tri_idx >= 0; }
};

class WorldBVH {
public:
    static WorldBVH& Get();

    // Build BVH from a flat triangle vertex buffer (3 * float[3] per tri).
    // verts layout: [x0,y0,z0, x1,y1,z1, x2,y2,z2, ...] — tri_count × 9 floats.
    // Invalidates any previous BVH. Safe to call multiple times (map reload).
    void Build(const float* verts, int tri_count);

    // Release GPU / CPU BVH data.
    void Shutdown();

    // Ray intersection query.
    //   origin[3], dir[3]: world-space ray (dir need not be normalized).
    //   max_t: maximum ray distance (default 1e30).
    // Returns WorldRayHit; hit.hit() == true when a triangle was intersected.
    WorldRayHit RayIntersect(const float* origin, const float* dir,
                              float max_t = 1e30f) const;

    bool IsBuilt() const { return built_; }
    int  TriCount() const { return tri_count_; }

private:
    // Opaque pointer to BVH implementation (avoids pulling tinybvh into all TUs).
    void* bvh_   = nullptr;
    float* verts_ = nullptr;  // owned copy of vertex data
    int   tri_count_ = 0;
    bool  built_     = false;
};

} // namespace md
