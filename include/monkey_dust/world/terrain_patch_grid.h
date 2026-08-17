#pragma once
#include <cstddef>

// Full-variant Phase 4 (plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md): Geomipmapping
// patch-LOD structure feeding the Phase 5 non-indexed ("Terra without a
// vertex buffer") patch renderer. This is a NEW file, not a git-revert of
// the deleted TerrainPatchGrid (engine commit 65859fe95..f8b93b4~1,
// removed by the GEOCLIPMAP cutover) -- the LOD float, neighbor-tier
// cascade, height-range relief bias, and per-patch AABB culling algorithms
// are ported by APPROACH from that history (same formulas, same
// rationale), but the OUTPUT contract is different: the old grid fed
// TerrainPatchRenderer::BuildInstanceBatches, which picked one of several
// separate STATIC meshes per tier; Tier() here instead selects a
// `quadsPerEdge` value consumed by ONE shared vertex-buffer-less shader
// (terrain_patch_novbo_spike.vert's production successor) via a per-patch
// UBO push -- there are no static meshes to batch by tier anymore.
//
// New in this version (Phase 0's crack-avoidance decision, not present in
// the old grid): SkirtDepth(ix,iz), reusing the same per-patch HeightRange
// this grid already tracks for AABB culling. The OLD grid's crack-avoidance
// was neighbor-tier cascade ALONE (max 1-tier difference between
// neighbors) -- proven historically insufficient on its own (task #389's
// fuzz-test counterexample chain, see Tier()'s doc comment below):
// cascade bounds how MUCH tiers can differ, but even a 1-tier difference
// still leaves a real T-junction gap (a finer neighbor has 2x the vertex
// density along the shared edge). Skirts close that gap structurally,
// independent of the cascade -- this grid keeps the cascade too (it still
// matters for shading/normal continuity), but the crack-avoidance
// guarantee now comes from the skirt, not the cascade.

class TerrainPatchGrid {
public:
    // Matches TerrainAtlas_SampleWorld's signature -- see the old grid's
    // own doc comment for why (real call sites can pass that function
    // pointer directly; nullptr skips height sampling entirely for
    // callers/tests with no atlas loaded, e.g. test_terrain_patch_grid.cpp).
    using HeightSampleFn = float (*)(float world_x, float world_z);

    // world_origin_x/z: world-space min corner (this project's world is
    // 0-based, [0, 64*CHUNK_SIZE), not centred on origin).
    // world_extent: TerrainWorldHeightmap::WorldExtent().
    // patch_size: world-space edge length of one patch. 300m matches
    // TerrainClipmapCache::kMeshQuads(128)*kBaseTexelSize's own level-0
    // footprint AND terrain_patch_novbo_spike.vert's spike convention --
    // not a hard requirement, but keeps this grid directly comparable
    // against the existing indexed system at the same density.
    // max_tier: highest tier index; tier t's quadsPerEdge = base >> t
    // (Phase 5's renderer owns the actual quadsPerEdge mapping -- this
    // grid only produces the tier INDEX, same separation of concerns the
    // old grid had between LOD selection and mesh-tier consumption).
    void Init(float world_origin_x, float world_origin_z, float world_extent,
              float patch_size, int max_tier, HeightSampleFn height_sampler = nullptr);

    int   NumPatchesX() const { return nx_; }
    int   NumPatchesZ() const { return nz_; }
    float PatchSize()   const { return patch_size_; }
    int   MaxTier()      const { return max_tier_; }

    float PatchOriginX(int ix) const { return world_origin_x_ + (float)ix * patch_size_; }
    float PatchOriginZ(int iz) const { return world_origin_z_ + (float)iz * patch_size_; }

    // Recomputes every patch's LOD float + cascaded tier from the given
    // camera position -- call once per frame, before SelectVisible.
    // O(nx_*nz_) scalar math, no allocation.
    void UpdateLOD(const float cam_pos[3]);

    float LOD(int ix, int iz) const { return lod_[(size_t)iz * nx_ + ix]; }

    // Final, already-neighbor-cascaded tier. See the file-level doc
    // comment above for why the cascade ALONE is not this grid's
    // crack-avoidance guarantee anymore (SkirtDepth is) -- the cascade
    // is kept because a >1-tier jump between neighbors still produces a
    // visibly wrong normal/shading discontinuity even once the skirt
    // hides the geometric gap. Ported by approach from the old grid's
    // Gauss-Seidel relaxation (task #389 fuzz-test-found 3-patch-chain
    // counterexample to a single-hop clamp, 2026-08-12): a value can
    // only ever decrease, so this provably terminates in at most
    // max_tier_ passes.
    int Tier(int ix, int iz) const { return tier_[(size_t)iz * nx_ + ix]; }

    // World-space (max-min) height sampled inside this patch's footprint
    // at Init() time (0 if height_sampler=nullptr, or the patch is
    // genuinely flat).
    float HeightRange(int ix, int iz) const { return height_range_[(size_t)iz * nx_ + ix]; }
    // Sampled minimum; HeightMin(ix,iz) + HeightRange(ix,iz) = sampled max.
    float HeightMin(int ix, int iz) const { return height_min_[(size_t)iz * nx_ + ix]; }

    // Phase 0's crack-avoidance sizing: reuses HeightRange (the same
    // value already sampled for AABB culling) rather than a global
    // worst-case constant -- measured on the real Kenshi heightmap,
    // global max |Δheight| per texel-step reaches 331-679m depending on
    // tier, so a single constant skirt depth for every patch would be
    // absurdly wasteful for the overwhelmingly flat majority. A flat
    // patch gets skirt≈margin; a cliff patch gets a skirt proportional
    // to its OWN real local relief.
    static constexpr float kSkirtMarginM = 5.0f;
    float SkirtDepth(int ix, int iz) const { return HeightRange(ix, iz) + kSkirtMarginM; }

    struct VisiblePatch {
        int   ix, iz;
        float origin_x, origin_z;
        float lod;
        int   tier;
    };
    // 4-side-plane AABB frustum cull (same convention MdCamera::
    // FrustumPlanes / the old grid's own AabbInFrustum used). Returns the
    // count written to out[] (capped at max_out).
    int SelectVisible(const float frustum_planes[16], VisiblePatch* out, int max_out,
                       float max_lod_cull = 1e9f) const;

    // Hard upper bound on total patches -- public so callers can size
    // out[]/max_out to this exactly (avoids the old system's
    // silently-drops-patches-in-raster-order bug class, 2026-08-09).
    static constexpr int kMaxPatchesPublic = 16384;

private:
    float world_origin_x_ = 0.f, world_origin_z_ = 0.f;
    float patch_size_     = 1.f;
    int   nx_ = 0, nz_ = 0;
    int   max_tier_ = 0;

    static constexpr float kAabbMinY = -500.f;
    static constexpr float kAabbMaxY = 4000.f;
    static constexpr float kAabbSafetyMarginM = 100.f;

    static constexpr int kMaxPatches = kMaxPatchesPublic;
    float lod_[kMaxPatches] = {};
    int   tier_[kMaxPatches] = {};
    float height_range_[kMaxPatches] = {};
    float height_min_[kMaxPatches] = {};
    bool  has_height_data_ = false;
};
