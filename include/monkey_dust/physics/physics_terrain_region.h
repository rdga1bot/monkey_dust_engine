#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

// Quadtree-LOD terrain rewrite, Phase 6 (see plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md). Independent,
// LOD-free physics-only terrain collision — a small fixed window of Jolt
// heightfield bodies around a moving focus point (player position in
// practice), decoupled from render chunk streaming entirely. Built
// directly from TerrainAtlas (the same full-world CPU heightmap Phase 5/9
// already redirected height/walkability queries to), not
// TerrainChunk::heightmap.h copies — physics doesn't need LOD at all, only
// correct height in a small radius around active actors.
//
// Coordinate mapping: same convention Phase 5/9 already established for
// TerrainQuery — bodies live in the caller's session-local floating
// coordinate space (see SceneRender::tnoff_x/z's own doc comment for why
// that's not the same as absolute Kenshi metres), sampled from
// TerrainAtlas's absolute-metres space via a constant offset. SetOrigin
// must be called at the same sites SceneRender/zone_transition.cpp already
// call TerrainQuery::Get().Init() at (chunk-streaming shifts, zone
// transitions) — kept as a SEPARATE call (not folded into TerrainQuery)
// so this stays a pure engine/physics concern with zero render coupling,
// matching this project's own split-readiness rule (game logic files like
// logic_tick.cpp never include render/scene_render.h).
class PhysicsTerrainRegion {
public:
    static PhysicsTerrainRegion& Get();

    static constexpr int   kSpan     = 3;      // 3x3 cells (~1382m square) — physics needs correct height near active actors only, not render draw distance
    static constexpr int   kGrid     = 128;    // TERRAIN_GRID — same resolution as the render mesh, no reason to differ
    static constexpr float kCellSize = 460.8f; // CHUNK_SIZE

    void SetOrigin(float session_origin_x, float session_origin_z,
                   float absolute_origin_x, float absolute_origin_z);

    // Call once per logic tick with the focus point (session-local
    // metres, e.g. player position). Shifts the window if the focus point
    // crossed into a new cell — cheap no-op most ticks. No-op entirely
    // until SetOrigin has been called at least once.
    void Update(float focus_x, float focus_z);

    void Shutdown();

private:
    struct Cell {
        JPH::BodyID body;
        int cell_x = INT32_MIN; // world cell index this body currently covers
        int cell_z = INT32_MIN; // sentinel (INT32_MIN) = not built yet
    };
    void RebuildCell(Cell& c, int new_cell_x, int new_cell_z);

    Cell  cells_[kSpan * kSpan];
    float session_origin_x_  = 0.f;
    float session_origin_z_  = 0.f;
    float absolute_origin_x_ = 0.f;
    float absolute_origin_z_ = 0.f;
    int   center_cell_x_     = INT32_MIN;
    int   center_cell_z_     = INT32_MIN;
    bool  ready_             = false;
};
