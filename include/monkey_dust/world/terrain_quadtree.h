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

        // docs/OPENMW_TERRAIN_BORROWED_TECHNIQUES.md Phase 2. use_stitched_mesh
        // is set unconditionally to (LodMode()==1) for every node -- an
        // explicit, unambiguous signal the renderer needs since
        // stitch_edge_mask==0 is otherwise indistinguishable between "mode
        // 0, always uses skirts" and "mode 1, zero coarser neighbors,
        // stitched IBO variant 0 is fine". Bit 0=north 1=south 2=east
        // 3=west (matches BuildTerrainQuadtreeStitchedIndices's own
        // edgeMask convention) -- set when that edge borders a neighbor
        // exactly 1 depth level coarser and needs the zippered stitch.
        // needs_skirt_fallback is set when any edge's gap is >=2 levels
        // (rare -- see QuadtreeBalanceNeighbors's "no gap, or gap<2"
        // comment) -- the stitched IBO can't represent that; the renderer
        // must fall back to the old filled_ibo_+skirt_ibo_ draw for this
        // node even though use_stitched_mesh is true.
        bool    use_stitched_mesh = false;
        uint8_t stitch_edge_mask = 0;
        bool    needs_skirt_fallback = false;
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
    //
    // B2 (Ulrich activation-level-propagation finding, RENDER_VS_ULRICH_
    // CHUNKLOD_DEEPSEEK_RESEARCH.md): after the recursive selection above,
    // an internal post-pass (QuadtreeBalanceNeighbors, terrain_quadtree.cpp)
    // widens a node's skirt_depth when one of its 4 edge neighbors is
    // covered only by an ancestor >=2 levels coarser -- using that coarse
    // neighbor's own already-measured relief as the target, since that is
    // the real vertical discontinuity needing a deeper skirt to hide. Pure
    // data adjustment on already-emitted nodes (no new nodes, no geometry
    // changes) -- see the .cpp doc comment for why inserting new nodes was
    // tried and rejected (it draws overlapping geometry on top of the
    // still-emitted coarse ancestor).
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

    // B1 (Ulrich screen-space-error finding, RENDER_VS_ULRICH_CHUNKLOD_
    // DEEPSEEK_RESEARCH.md's lod_selection_math topic): the threshold above
    // (`size * detail_multiplier`) was completely FOV/resolution-independent
    // -- a 4K 90-FOV view and an 800x600 45-FOV view selected identical
    // nodes at identical camera positions, which is provably wrong (higher
    // resolution resolves smaller angular error; narrower FOV maps the same
    // angular error to a smaller world-space distance). Call once per frame
    // with the real screen width (px) and vertical FOV (degrees) before
    // SelectVisible; SelectVisible scales detail_multiplier_ by
    // (screen_width/1280) * (tan(22.5deg)/tan(fovy/2)) so that at this
    // project's reference config (1280px, fovy=45 -- Intel HD 520 target
    // resolution, game_init.cpp's camera.fovy default) it reproduces
    // task #400's already-tuned numeric behaviour exactly (never called ==
    // identical to today). Deliberately does NOT attempt per-node relief-
    // based error scaling (the "flat area and a cliff get the same LOD"
    // half of the same finding) -- using the already-sampled height_range
    // as an error term was tried during implementation and rejected: with
    // realistic screen/FOV constants the projected acceptable-range comes
    // out several thousand metres even for near-flat terrain (this
    // codebase's whole-footprint relief is orders of magnitude cruder than
    // Ulrich's real per-level baked vertex-displacement error), which would
    // need real live-tuned calibration this session couldn't do responsibly
    // blind. That refinement is a separate, explicitly deferred follow-up.
    void SetScreenParams(float screen_width_px, float fovy_degrees);

    // 2026-08-28 (docs/OPENMW_TERRAIN_BORROWED_TECHNIQUES.md Phase 1): OpenMW's
    // components/terrain/quadtreeworld.cpp DefaultLodCallback uses a discrete
    // log2(dist/(minSize*factor)) vs log2(size/minSize) bucket comparison
    // instead of this class's continuous `dist < size*detail_multiplier`
    // range test. Ported here as an OFF-by-default alternative for direct
    // A/B, not a replacement -- the existing range+morph test is already
    // more sophisticated (continuous distance response, real geomorph,
    // B1's FOV/resolution-aware scaling); the OpenMW version has none of
    // that in this port (no morph term at all -- see RecurseNode's mode==1
    // branch). Mode 0 = existing (default), 1 = OpenMW-style discrete.
    void SetLodMode(int mode) { lod_mode_ = mode; }
    int LodMode() const { return lod_mode_; }

private:
    float world_origin_x_ = 0.f, world_origin_z_ = 0.f;
    float world_extent_   = 0.f;
    float chunk_size_     = 460.8f;
    int   max_depth_      = 3;
    float detail_multiplier_ = 3.0f;
    int   lod_mode_ = 0; // 0=default range+morph, 1=OpenMW-style discrete log2
    HeightSampleFn height_sampler_ = nullptr;
    float screen_width_px_ = 1280.f;
    float fovy_degrees_    = 45.f;
};
