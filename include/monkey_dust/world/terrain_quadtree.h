#pragma once

// Quadtree-LOD terrain rewrite, Phase 1+2 (see plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md). Pure CPU spatial
// structure only — a node is just a bbox + depth, no owned geometry (the
// same shared unit-patch mesh is reused at every depth via a per-instance
// world offset+scale, see TerrainQuadtreeRenderer).
struct TerrainQuadNode {
    float origin_x = 0.f;   // world-space min-X corner, metres
    float origin_z = 0.f;   // world-space min-Z corner, metres
    float size     = 0.f;   // node extent (square), metres
    int   depth    = 0;     // 0 = root
};

// Phase 2: a node selected for drawing by SelectVisible — either a leaf
// (depth == MaxDepth()) or an internal node whose LOD distance threshold
// says its own detail level already suffices at the current camera range,
// so traversal stopped without recursing into its 4 children.
struct TerrainQuadVisibleNode {
    float origin_x = 0.f;
    float origin_z = 0.f;
    float size     = 0.f;
    int   depth    = 0;
};

class TerrainQuadtree {
public:
    static constexpr int MAX_VISIBLE_NODES = 512;

    // world_size_m: full square world extent centred on origin (0,0).
    // max_depth: how many times SelectVisible may recurse past the root
    // (root itself is depth 0); leaf node size = world_size_m / 2^max_depth.
    void Init(float world_size_m, int max_depth);

    // Same as above but with an explicit root min-corner instead of always
    // centring on (0,0) — needed to match a TerrainQuadtreeRenderer's own
    // region origin, which lives in whatever session-local floating
    // coordinate space the caller's world streaming uses (see
    // SceneRender::tnoff_x/z's own doc comment for why that's not the same
    // as "absolute world metres").
    void Init(float origin_x, float origin_z, float world_size_m, int max_depth);

    const TerrainQuadNode& Root() const { return root_; }
    int MaxDepth() const { return max_depth_; }

    // Phase 2: synchronous recursive traversal — frustum cull + LOD
    // distance criterion. No persistent tree is built; children are
    // computed on the fly at each recursion step (a real quadtree here
    // would need up to 4^max_depth nodes' worth of storage for nothing —
    // the CDLOD technique this is based on famously doesn't need one).
    //
    // frustum_planes: 4 side planes (left,right,top,bottom) in the format
    // MdCamera::FrustumPlanes fills — vec4[4], point-inside test is
    // dot(plane.xyz, point) + plane.w >= 0. No near/far plane; distance
    // culling is handled separately by lod_distances' own outermost entry.
    //
    // lod_distances: MaxDepth()+1 entries. lod_distances[d] is the camera
    // distance (world units, not squared) below which a depth-d node
    // recurses into its 4 children instead of being drawn as-is; must be
    // monotonically decreasing with depth (root's threshold is the
    // largest). The last entry (index MaxDepth()) is never read — a node
    // already at MaxDepth() always draws, never recurses further.
    //
    // Writes up to MAX_VISIBLE_NODES entries into out[], returns the
    // actual count written. If traversal would exceed MAX_VISIBLE_NODES,
    // the remainder is silently dropped (no dynamic growth in this
    // hot-path-adjacent call) — callers needing to know should compare the
    // return value against MAX_VISIBLE_NODES.
    int SelectVisible(const float cam_pos[3], const float frustum_planes[16],
                       const float* lod_distances,
                       TerrainQuadVisibleNode* out) const;

private:
    TerrainQuadNode root_;
    int             max_depth_ = 0;
};
