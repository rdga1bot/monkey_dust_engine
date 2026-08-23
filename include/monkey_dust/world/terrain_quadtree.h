#pragma once

// Final terrain architecture (2026-08-18, "Ogre-quadtree (geomorph+skirts)",
// serene-pondering-teapot.md) -- CPU-side LOD selection. NO persistent tree
// is stored: every frame, SelectVisible() recomputes visible nodes purely
// from camera position via a recursive per-zone traversal. This is
// deliberate (matches this project's own "fully resident, no streaming
// needed" conclusion from the DeepSeek architecture review) -- a real
// Ogre::TerrainGroup streams/pages terrain because it can't assume the
// whole world fits in memory; ours already does (TerrainWorldHeightmap is
// one static, fully-resident VTF texture), so there's nothing to page.
//
// Quadtree roots are aligned to real Kenshi zones (CHUNK_SIZE=460.8m each,
// re_docs/kenshi/terrain.md's own zone-tile convention) -- depth 0 is a
// whole zone (16x16 quads, texelSize=28.8m), depth 3 is a leaf (16x16
// quads, texelSize=3.6m == TERRAIN_STEP, the native heightmap texel
// spacing). Every node at every depth uses the SAME shared index buffer
// (terrain_quadtree_mesh.h) -- only origin/size/morph differ per draw.
class TerrainQuadtree {
public:
    struct VisibleNode {
        float origin_x, origin_z; // world-space min corner
        float size;               // world-space edge length (16 quads span this)
        int   depth;              // 0 = zone root (coarsest), max_depth = finest leaf
        float morph;              // 0..1 continuous: geomorph blend toward this
                                   // node's OWN parent shape (see .cpp for the
                                   // exact CDLOD-style formula) -- 0 near camera,
                                   // 1 at the boundary where a coarser sibling
                                   // would have been drawn instead.
        float skirt_depth;        // per-node, from HeightRange sampled at Init
    };

    using HeightSampleFn = float (*)(float world_x, float world_z);

    // world_extent: TerrainWorldHeightmap::WorldExtent() (must be an exact
    // multiple of CHUNK_SIZE -- true for the real 64-zone Kenshi world).
    // max_depth: 3 matches CHUNK_SIZE(460.8)/patchSize(16)/2^3 = TERRAIN_STEP
    // exactly; kept as a parameter for tests, not meant to be tuned in
    // production without re-deriving that match.
    // detail_multiplier: CDLOD-style "how many node-widths away before this
    // node simplifies" constant -- larger = finer detail persists farther
    // (more geometry), smaller = coarsens sooner (less geometry, more
    // popping risk if too small since the morph transition band shrinks
    // proportionally too). 3.0 is a reasonable starting point, matching
    // typical CDLOD tuning in the public literature this session's DeepSeek
    // research cited.
    void Init(float world_origin_x, float world_origin_z, float world_extent,
              float chunk_size, int max_depth, float detail_multiplier,
              HeightSampleFn height_sampler);

    // 4 side frustum planes (MdCamera::FrustumPlanes convention, no far
    // plane) + camera position. Returns count written to out[] (capped at
    // max_out). Deterministic function of cam_pos/frustum -- safe to call
    // every frame with no state carried between calls.
    int SelectVisible(const float cam_pos[3], const float frustum_planes[16],
                       VisibleNode* out, int max_out) const;

    static constexpr int kMaxNodesPublic = 16384;

    // #400 (tier-threshold tuning, 2026-08-23): live override for
    // detail_multiplier, read by SelectVisible/RecurseNode every frame (no
    // persistent tree state to invalidate) -- lets md.set_terrain_detail_
    // multiplier() A/B different subdivide thresholds without a rebuild.
    // NOT for shipping -- see game/src/render/scene_render.h's field doc
    // comment for the full ablation rationale, same pattern as
    // terrain_shade_constant_debug.
    void SetDetailMultiplier(float v) { detail_multiplier_ = v; }
    float DetailMultiplier() const { return detail_multiplier_; }

private:
    float world_origin_x_ = 0.f, world_origin_z_ = 0.f;
    float world_extent_   = 0.f;
    float chunk_size_     = 460.8f;
    int   max_depth_      = 3;
    float detail_multiplier_ = 3.0f;
    HeightSampleFn height_sampler_ = nullptr;
};
