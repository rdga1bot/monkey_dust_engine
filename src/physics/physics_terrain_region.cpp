#include <monkey_dust/physics/physics_terrain_region.h>
#include <monkey_dust/physics/jolt_world.h>
#include <monkey_dust/world/terrain_gen.h>
#include <cmath>

PhysicsTerrainRegion& PhysicsTerrainRegion::Get() {
    static PhysicsTerrainRegion s;
    return s;
}

void PhysicsTerrainRegion::SetOrigin(float session_origin_x, float session_origin_z,
                                      float absolute_origin_x, float absolute_origin_z) {
    // Idempotent: callers (chunk-streaming shift, async chunk consumption,
    // zone transition) may call this many times per second with the SAME
    // mapping — only force a rebuild when it actually changed, otherwise
    // this would re-sample+rebuild all kSpan*kSpan cells on every call for
    // no reason.
    bool changed = !ready_
        || session_origin_x_  != session_origin_x
        || session_origin_z_  != session_origin_z
        || absolute_origin_x_ != absolute_origin_x
        || absolute_origin_z_ != absolute_origin_z;
    session_origin_x_  = session_origin_x;
    session_origin_z_  = session_origin_z;
    absolute_origin_x_ = absolute_origin_x;
    absolute_origin_z_ = absolute_origin_z;
    ready_ = true;
    if (changed) {
        // Force every cell to rebuild on the next Update() — any existing
        // bodies were built against the OLD mapping and would now sit at
        // the wrong session-local position.
        center_cell_x_ = INT32_MIN;
        center_cell_z_ = INT32_MIN;
    }
}

void PhysicsTerrainRegion::RebuildCell(Cell& c, int new_cell_x, int new_cell_z) {
    if (!c.body.IsInvalid()) {
        JoltWorld::Get().RemoveBody(c.body);
        c.body = JPH::BodyID();
    }
    // Bug fix (2026-08-08): cell_x/cell_z used to be set BEFORE this check,
    // so if the atlas wasn't loaded yet (very plausible right at spawn --
    // this runs every physics tick from the first tick), the cell would be
    // marked "built" at this position with body left invalid, and Update()
    // (which only calls RebuildCell when cell_x/cell_z DON'T match) would
    // never retry it again -- a permanent hole in terrain collision with
    // real rendered terrain visible above it (character falls straight
    // through, "провалюється під термейн" bug report). Leaving cell_x/
    // cell_z at their INT32_MIN "not built yet" sentinel (header's own
    // documented meaning) here means Update() keeps calling RebuildCell
    // every tick until the atlas loads and the body is actually created.
    if (!TerrainAtlas_Loaded()) return;
    c.cell_x = new_cell_x;
    c.cell_z = new_cell_z;

    static float s_heights[(kGrid + 1) * (kGrid + 1)];
    const float step = kCellSize / (float)kGrid;
    const float abs_ox = (float)new_cell_x * kCellSize;
    const float abs_oz = (float)new_cell_z * kCellSize;
    for (int row = 0; row <= kGrid; ++row) {
        for (int col = 0; col <= kGrid; ++col) {
            s_heights[row * (kGrid + 1) + col] =
                TerrainAtlas_SampleWorld(abs_ox + (float)col * step, abs_oz + (float)row * step);
        }
    }

    // Session-local placement: same constant-offset translation Phase 5/9
    // use for TerrainQuery (see this class's own doc comment).
    float session_ox = abs_ox - absolute_origin_x_ + session_origin_x_;
    float session_oz = abs_oz - absolute_origin_z_ + session_origin_z_;
    c.body = JoltWorld::Get().AddTerrainMesh(s_heights, kGrid, step, session_ox, session_oz);
}

void PhysicsTerrainRegion::Update(float focus_x, float focus_z) {
    if (!ready_) return;
    float abs_focus_x = focus_x - session_origin_x_ + absolute_origin_x_;
    float abs_focus_z = focus_z - session_origin_z_ + absolute_origin_z_;
    int new_center_x = (int)floorf(abs_focus_x / kCellSize);
    int new_center_z = (int)floorf(abs_focus_z / kCellSize);
    if (new_center_x == center_cell_x_ && new_center_z == center_cell_z_) return;
    center_cell_x_ = new_center_x;
    center_cell_z_ = new_center_z;

    constexpr int half = kSpan / 2;
    for (int dz = -half; dz <= half; ++dz) {
        for (int dx = -half; dx <= half; ++dx) {
            int wx = new_center_x + dx, wz = new_center_z + dz;
            Cell& c = cells_[(dz + half) * kSpan + (dx + half)];
            if (c.cell_x != wx || c.cell_z != wz) RebuildCell(c, wx, wz);
        }
    }
}

void PhysicsTerrainRegion::Shutdown() {
    for (auto& c : cells_) {
        if (!c.body.IsInvalid()) {
            JoltWorld::Get().RemoveBody(c.body);
            c.body = JPH::BodyID();
        }
        c.cell_x = INT32_MIN;
        c.cell_z = INT32_MIN;
    }
    ready_ = false;
    center_cell_x_ = INT32_MIN;
    center_cell_z_ = INT32_MIN;
}
