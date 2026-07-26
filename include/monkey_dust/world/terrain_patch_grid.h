#pragma once
#include <cstddef>

// Granite-style terrain migration, Phase 2 (plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md): replaces
// TerrainQuadtree's recursive tree with a FLAT grid of fixed-size
// patches covering the whole world (TerrainWorldHeightmap's extent,
// Phase 1) — no recursion, no async traversal job. Each patch's LOD is a
// single float recomputed every frame directly from camera distance
// (O(num_patches), cheap enough at this patch count to never need the
// old system's TerrainQuadtreeAsyncSelector double-buffered job
// machinery, which was itself part of the old system's bug surface).
//
// LOD formula: dist_log2 = 0.5*log2(camera_distance^2), clamped to
// [0, max_lod] — a lower value means finer detail (more subdivided
// mesh); patches near the camera settle at LOD 0 (finest static mesh
// tier, Phase 3), patches far away climb toward max_lod (coarsest
// tier). This is a general, well-known distance-to-LOD mapping (log2 of
// squared distance avoids a sqrt), not tied to any one engine's
// implementation.
class TerrainPatchGrid {
public:
    // world_origin_x/z: world-space min corner the grid covers (match
    // TerrainWorldHeightmap's own convention — this project's world is
    // 0-based, [0, 64*CHUNK_SIZE), not centred on origin).
    // world_extent: full square world size (TerrainWorldHeightmap::
    // WorldExtent()). patch_size: world-space edge length of one patch
    // (~250-300m recommended — independent of the old CHUNK_SIZE=460.8m
    // zone boundary; patches don't need zone alignment).
    // max_lod: highest LOD tier index (Phase 3 builds max_lod+1 static
    // mesh tiers, halving resolution each level).
    void Init(float world_origin_x, float world_origin_z, float world_extent,
              float patch_size, int max_lod);

    int   NumPatchesX() const { return nx_; }
    int   NumPatchesZ() const { return nz_; }
    float PatchSize()   const { return patch_size_; }
    int   MaxLod()       const { return max_lod_; }

    float PatchOriginX(int ix) const { return world_origin_x_ + (float)ix * patch_size_; }
    float PatchOriginZ(int iz) const { return world_origin_z_ + (float)iz * patch_size_; }

    // Recomputes every patch's own LOD float from the given camera
    // position — call once per frame, synchronously, before
    // SelectVisible. O(nx_*nz_) scalar math, no allocation.
    void UpdateLOD(const float cam_pos[3]);

    float LOD(int ix, int iz) const { return lod_[(size_t)iz * nx_ + ix]; }
    // Neighbor LOD, clamped at grid edges (edge patches treat the
    // out-of-grid side as sharing their own LOD — matches this being the
    // world boundary, nothing to snap against there).
    float NeighborLOD(int ix, int iz, int dx, int dz) const {
        int nix = ix + dx, niz = iz + dz;
        if (nix < 0) nix = 0;
        if (nix >= nx_) nix = nx_ - 1;
        if (niz < 0) niz = 0;
        if (niz >= nz_) niz = nz_ - 1;
        return LOD(nix, niz);
    }

    struct VisiblePatch {
        int   ix, iz;   // grid indices (for NeighborLOD lookups)
        float origin_x, origin_z;
        float lod;
    };
    // Frustum-culls (4-plane AABB test, same convention MdCamera::
    // FrustumPlanes/TerrainQuadtree::SelectVisible's own AabbInFrustum
    // used) and fills out[] with every patch whose AABB is at least
    // partially in view. Returns the count (capped at max_out).
    //
    // max_lod_cull (default: no extra cull, matches every existing test
    // and caller unchanged): the 4 frustum planes here are SIDE planes
    // only (left/right/top/bottom, same convention MdCamera::
    // FrustumPlanes uses) -- there is no far-plane/distance test in that
    // convention anywhere in this codebase. Without an explicit distance
    // cutoff, patches many kilometres past any reasonable draw/fog
    // distance still pass the side-plane test and get fully vertex-
    // shaded (VTF height + normal sampling) only to be clipped by the
    // GPU afterward -- wasted work, and the actual real-world cause of a
    // measured "GPU load >80%" regression (patch(lod) already encodes
    // distance via UpdateLOD's own formula, so this reuses that value
    // directly instead of a redundant distance computation here).
    int SelectVisible(const float frustum_planes[16], VisiblePatch* out, int max_out,
                       float max_lod_cull = 1e9f) const;

private:
    float world_origin_x_ = 0.f, world_origin_z_ = 0.f;
    float patch_size_     = 1.f;
    int   nx_ = 0, nz_ = 0;
    int   max_lod_ = 0;
    // Generous fixed Y range for the frustum AABB test, same rationale
    // TerrainQuadtree::AabbInFrustum used: no per-patch height bound
    // dependency needed for a conservative box-vs-4-planes cull.
    static constexpr float kAabbMinY = -500.f;
    static constexpr float kAabbMaxY = 4000.f;

    // lod_[iz*nx_+ix] -- flat, not a vector<> to avoid heap churn on
    // resize; sized once in Init() via a fixed cap (see .cpp for the cap
    // and why it's generous for this world size).
    static constexpr int kMaxPatches = 16384;
    float lod_[kMaxPatches] = {};
};
