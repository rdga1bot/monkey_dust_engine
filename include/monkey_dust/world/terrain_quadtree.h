#pragma once

// 2026-09-05 (docs/TERRAIN_FLAT_LOD_PLAN.md): replaced the adaptive CDLOD
// quadtree (recursive per-frame subdivision, continuous geomorph, per-node
// relief-sampled skirts, hash-based neighbor balancing, 16 stitched-IBO
// variants -- ~450 lines across this file + terrain_quadtree.cpp +
// terrain_quadtree_mesh.cpp) with fixed-depth tiling: every visible zone is
// drawn at the SAME depth, so neighbors always share identical vertex
// density at their border and there is no LOD mismatch to hide -- skirts/
// stitching aren't a cheaper alternative here, they're structurally
// unnecessary. Validated on real hardware (Intel HD 520) across 4 diverse
// locations (steep canyon, hills, mountains, gentle terrain): pixel-
// identical output (RMSE<0.5, 0% changed pixels) vs the old adaptive system
// at native leaf resolution, while being 2.3-3.6x cheaper (RenderTotal) --
// the CPU cost of the adaptive system's own per-frame tree-walk + relief
// scan + neighbor balancing exceeded the GPU cost of just drawing more
// triangles at uniform resolution. Full evidence and rejected intermediate
// depths (1, 2 -- both showed real visual regressions on the worst-case
// test point) in that doc.
//
// Quadtree roots are aligned to real Kenshi zones (CHUNK_SIZE=460.8m each,
// re_docs/kenshi/terrain.md's own zone-tile convention). Every emitted tile
// reuses the SAME shared index buffer (terrain_quadtree_mesh.h) -- only
// origin/size differ per draw.
class TerrainQuadtree {
public:
    struct VisibleNode {
        float origin_x, origin_z; // world-space min corner
        float size;               // world-space edge length (16 quads span this)
        int   depth;              // fixed (kFlatLodDepth, terrain_quadtree.cpp)
    };

    using HeightSampleFn = float (*)(float world_x, float world_z);

    // world_extent: TerrainWorldHeightmap::WorldExtent() (must be an exact
    // multiple of CHUNK_SIZE -- true for the real 64-zone Kenshi world).
    void Init(float world_origin_x, float world_origin_z, float world_extent,
              float chunk_size, HeightSampleFn height_sampler);

    // 4 side frustum planes (MdCamera::FrustumPlanes convention, no far
    // plane) + camera position. Returns count written to out[] (capped at
    // max_out). Deterministic function of cam_pos/frustum -- safe to call
    // every frame with no state carried between calls.
    int SelectVisible(const float cam_pos[3], const float frustum_planes[16],
                       VisibleNode* out, int max_out) const;

    static constexpr int kMaxNodesPublic = 16384;

private:
    float world_origin_x_ = 0.f, world_origin_z_ = 0.f;
    float world_extent_   = 0.f;
    float chunk_size_     = 460.8f;
    HeightSampleFn height_sampler_ = nullptr;
};
