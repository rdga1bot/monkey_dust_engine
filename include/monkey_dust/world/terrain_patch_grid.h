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
    // Optional world-space height sampler (matches TerrainAtlas_SampleWorld's
    // own signature exactly, so real call sites can pass that function
    // pointer directly) -- used ONCE per patch at Init() time to precompute
    // each patch's local height RANGE, which UpdateLOD then uses to keep
    // high-relief patches at finer LOD for longer (see UpdateLOD's own doc
    // comment for why distance alone is not enough). Pass nullptr to skip
    // entirely (every patch's height_range_ stays 0, i.e. the exact old
    // distance-only behaviour) -- this keeps TerrainPatchGrid decoupled
    // from the TerrainAtlas global singleton for callers/tests that don't
    // have one loaded (see test_terrain_patch_grid.cpp, which never loads
    // an atlas and must keep working unmodified).
    using HeightSampleFn = float (*)(float world_x, float world_z);

    // world_origin_x/z: world-space min corner the grid covers (match
    // TerrainWorldHeightmap's own convention — this project's world is
    // 0-based, [0, 64*CHUNK_SIZE), not centred on origin).
    // world_extent: full square world size (TerrainWorldHeightmap::
    // WorldExtent()). patch_size: world-space edge length of one patch
    // (~250-300m recommended — independent of the old CHUNK_SIZE=460.8m
    // zone boundary; patches don't need zone alignment).
    // max_lod: highest LOD tier index (Phase 3 builds max_lod+1 static
    // mesh tiers, halving resolution each level).
    // height_sampler: see HeightSampleFn above.
    void Init(float world_origin_x, float world_origin_z, float world_extent,
              float patch_size, int max_lod, HeightSampleFn height_sampler = nullptr);

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

    // Final, already-neighbor-cascaded tier this patch should draw at --
    // see UpdateLOD's own doc comment for the cascade algorithm and why
    // it replaced BOTH the single-hop clamp task #389's first fix used
    // AND the even older whole-grid cascade-snap this same class removed
    // back on 2026-07-31 (see the historical note further down). Callers
    // (TerrainPatchRenderer::BuildInstanceBatches) should use THIS, not
    // floor(LOD(ix,iz)), to pick which mesh tier a patch draws with.
    int Tier(int ix, int iz) const { return tier_[(size_t)iz * nx_ + ix]; }

    // World-space (max-min) height sampled inside this patch's footprint at
    // Init() time (0 if Init() was called with height_sampler=nullptr, or
    // if the patch is genuinely flat) -- see HeightSampleFn's doc comment.
    float HeightRange(int ix, int iz) const { return height_range_[(size_t)iz * nx_ + ix]; }

    // World-space minimum height sampled inside this patch's footprint at
    // Init() time (0 if Init() was called with height_sampler=nullptr).
    // HeightMin(ix,iz) + HeightRange(ix,iz) gives the sampled maximum --
    // together these are what SelectVisible uses for a tight per-patch
    // AABB Y-range instead of the old whole-grid kAabbMinY/kAabbMaxY
    // constants (borrowed from Zylann/godot_heightmap_plugin's
    // HTerrainDataRangeMap, which does the same per-chunk min/max-for-
    // culling trick, 2026-08-12 -- see CLAUDE_STATE.md for the source
    // comparison this came from).
    float HeightMin(int ix, int iz) const { return height_min_[(size_t)iz * nx_ + ix]; }

    // History (why a cascade lives here again after being removed once):
    // task terrain-lod-seam-cascade-gap (2026-07-31) had a neighbor-tier
    // cascade-snap here; task terrain-cdlod-geomorph (same day) removed
    // it in favour of the per-patch CDLOD-style geomorph alone (each
    // patch morphs its own vertices toward its next-coarser tier's grid,
    // terrain_patch_gbuffer.vert's main()) -- an A/B test at the time
    // found the one specific reported crack was completely UNCHANGED by
    // the cascade-snap, so it looked unnecessary. That test predates the
    // height-range relief bias (task #387): a per-patch, DATA-dependent
    // LOD term (see UpdateLOD below) that makes a real >1-tier neighbor
    // gap common wherever a patch straddles a cliff, not the rare edge
    // case the old test happened to check. Task #389 (2026-08-11) first
    // patched this with a SINGLE-HOP clamp in the caller (compare each
    // patch's tier against each neighbor's RAW, pre-clamp tier) -- fixed
    // the reported live repro, but a follow-up fuzz test (2026-08-12,
    // TerrainPatchRendererBatching.FuzzRandomCliffsPreserveInvariants)
    // found real counterexamples within ~10 random iterations: a 3-patch
    // chain (cliff A -- flat B -- cliff C) where B's single-hop clamp
    // uses C's raw tier and lands B two tiers below A, but A's own
    // single-hop clamp never learns B's FINAL (already-clamped) tier, so
    // A and B end up 2 tiers apart despite each individually "respecting"
    // its neighbors. The cascade below closes that gap: it repeatedly
    // relaxes tier_[] against already-updated neighbor values (values can
    // only decrease, floored at 0, so this provably terminates in at
    // most max_lod_ passes) instead of a single pass against stale data.

    struct VisiblePatch {
        int   ix, iz;   // grid indices
        float origin_x, origin_z;
        float lod;   // raw continuous LOD -- geomorph fract() source
        int   tier;  // final, already-cascaded tier -- see Tier() above
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

    // Hard upper bound on total patches this grid can ever hold (see
    // kMaxPatches below) -- public so SelectVisible callers can size their
    // own `out[]`/max_out buffer to this exactly and make truncation
    // (silently dropping patches past a too-small max_out, in raster scan
    // order with no distance sort -- real bug, 2026-08-09, editor World3D
    // aerial view) provably impossible instead of picking an arbitrary
    // smaller cap and hoping it's never exceeded.
    static constexpr int kMaxPatchesPublic = 16384;

private:
    float world_origin_x_ = 0.f, world_origin_z_ = 0.f;
    float patch_size_     = 1.f;
    int   nx_ = 0, nz_ = 0;
    int   max_lod_ = 0;
    // Fallback Y range for the frustum AABB test when Init() was called
    // with no height_sampler (test/tool callers that never loaded real
    // height data, see HeightSampleFn's doc comment) -- same rationale
    // TerrainQuadtree::AabbInFrustum originally used: a conservative
    // whole-world box-vs-4-planes cull with no per-patch dependency.
    // When a height_sampler WAS provided, SelectVisible uses the tighter
    // per-patch [HeightMin, HeightMin+HeightRange] bounds instead (plus
    // kAabbSafetyMarginM below) -- these constants stay only as that
    // fallback.
    static constexpr float kAabbMinY = -500.f;
    static constexpr float kAabbMaxY = 4000.f;
    // Padding added to the sampled per-patch [min,max] before using it
    // for culling -- the 5x5 relief sample grid (Init(), kReliefSampleGrid
    // in the .cpp) is NOT exhaustive, so a peak/trough between two
    // adjacent 75m-spaced samples could go undetected. This margin is a
    // heuristic, not a proven bound: no formal Lipschitz/smoothness
    // guarantee exists for arbitrary height data. Chosen generously
    // relative to real measured Kenshi relief (~500m over a 300m patch
    // is the most extreme case this codebase has recorded, see task
    // #387's own comments) -- an undetected excursion bigger than this
    // between two 75m-apart samples would be a genuinely pathological
    // (near-vertical, sub-75m-wide) spike, not realistic DEM-derived
    // terrain. If real terrain data is ever found to violate this,
    // widen the margin or shrink kReliefSampleGrid's spacing -- do not
    // just delete the margin.
    static constexpr float kAabbSafetyMarginM = 100.f;

    // lod_[iz*nx_+ix] -- flat, not a vector<> to avoid heap churn on
    // resize; sized once in Init() via a fixed cap (see .cpp for the cap
    // and why it's generous for this world size). Must equal
    // kMaxPatchesPublic above (single source of truth would need C++17
    // inline variables across a constexpr boundary that isn't worth the
    // churn here -- just keep these two literals in sync).
    static constexpr int kMaxPatches = kMaxPatchesPublic;
    float lod_[kMaxPatches] = {};
    // tier_[iz*nx_+ix] -- final, already-cascaded tier (see Tier()'s doc
    // comment and UpdateLOD's cascade algorithm). Recomputed every
    // UpdateLOD() call, same cadence as lod_[].
    int   tier_[kMaxPatches] = {};
    // height_range_[iz*nx_+ix] -- (max-min) world-space height sampled
    // inside patch (ix,iz)'s footprint at Init() time; 0 if Init() was
    // called with height_sampler=nullptr. See HeightSampleFn's doc comment.
    float height_range_[kMaxPatches] = {};
    // height_min_[iz*nx_+ix] -- sampled minimum, paired with height_range_
    // above (max = min+range). See HeightMin()'s doc comment.
    float height_min_[kMaxPatches] = {};
    // Set at Init() time: true iff a non-null height_sampler was passed,
    // i.e. height_min_/height_range_ hold real sampled data and
    // SelectVisible should use them for a tight per-patch AABB instead of
    // the whole-world kAabbMinY/kAabbMaxY fallback.
    bool  has_height_data_ = false;
};
