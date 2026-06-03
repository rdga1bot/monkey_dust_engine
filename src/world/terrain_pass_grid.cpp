#include <monkey_dust/world/terrain_pass_grid.h>
#include <monkey_dust/nav/navmesh.h>

// ── TerrainPassGrid_Build ─────────────────────────────────────────────────────
// For each 64×64 cell: query NavMesh at cell centre (Y=0 probe).
// A cell is walkable if Detour finds a valid polygon.

int TerrainPassGrid_Build(TerrainPassGrid& grid, const NavMesh& nav,
                           float chunk_origin_x, float chunk_origin_z) {
    grid.Clear();
    int count = 0;
    float out[3];

    for (int row = 0; row < PASS_GRID_N; ++row) {
        for (int col = 0; col < PASS_GRID_N; ++col) {
            float wx = chunk_origin_x + (col + 0.5f) * PASS_CELL_SIZE;
            float wz = chunk_origin_z + (row + 0.5f) * PASS_CELL_SIZE;
            // FindPath(same→same): returns 1 point if start poly exists.
            if (nav.FindPath(wx, 0.f, wz, wx, 0.f, wz, out, 1) > 0) {
                grid.SetWalkable(row, col);
                ++count;
            }
        }
    }
    return count;
}

// ── TerrainPassGrid_HasLOS ────────────────────────────────────────────────────
// Bresenham line through the bitmask. All cells on path must be walkable.

bool TerrainPassGrid_HasLOS(const TerrainPassGrid& grid,
                              float lx0, float lz0,
                              float lx1, float lz1) noexcept {
    auto clamp_cell = [](int v) {
        return v < 0 ? 0 : v >= PASS_GRID_N ? PASS_GRID_N - 1 : v;
    };
    int x0 = clamp_cell((int)(lx0 / PASS_CELL_SIZE));
    int y0 = clamp_cell((int)(lz0 / PASS_CELL_SIZE));
    int x1 = clamp_cell((int)(lx1 / PASS_CELL_SIZE));
    int y1 = clamp_cell((int)(lz1 / PASS_CELL_SIZE));

    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dz = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sz = (y0 < y1) ? 1 : -1;
    int err = dx - dz;

    for (;;) {
        if (!grid.IsWalkable(y0, x0)) return false;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dz) { err -= dz; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sz; }
    }
    return true;
}
