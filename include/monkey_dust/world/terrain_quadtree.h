#pragma once

// Quadtree-LOD terrain rewrite, Phase 1 (see plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md). Pure CPU spatial
// structure only — a node is just a bbox + depth, no owned geometry (the
// same shared unit-patch mesh is reused at every depth via a per-instance
// world offset+scale, see TerrainQuadtreeRenderer). Phase 1 only builds the
// root; recursive split/traversal/frustum-cull/LOD-selection is Phase 2.
struct TerrainQuadNode {
    float origin_x = 0.f;   // world-space min-X corner, metres
    float origin_z = 0.f;   // world-space min-Z corner, metres
    float size     = 0.f;   // node extent (square), metres
    int   depth    = 0;     // 0 = root
};

class TerrainQuadtree {
public:
    // world_size_m: full square world extent centred on origin (0,0).
    // max_depth: reserved for Phase 2's recursive split; unused until then.
    void Init(float world_size_m, int max_depth);

    const TerrainQuadNode& Root() const { return root_; }
    int MaxDepth() const { return max_depth_; }

private:
    TerrainQuadNode root_;
    int             max_depth_ = 0;
};
