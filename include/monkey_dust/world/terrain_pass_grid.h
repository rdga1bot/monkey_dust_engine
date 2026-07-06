#pragma once
// terrain_pass_grid.h does NOT include terrain_chunk.h to avoid circular deps.
// terrain_chunk.h includes this file and adds TerrainPassGrid as a field.
#include <cstdint>
#include <cstring>
#include <monkey_dust/world/chunk_def.h>

// Grid resolution matches TERRAIN_GRID in terrain_chunk.h (64 cells per axis).
// PASS_CELL_SIZE = CHUNK_SIZE / PASS_GRID_N(64); derived from CHUNK_SIZE directly
// (not a duplicated literal) so the two stay in sync if CHUNK_SIZE ever changes again.
static constexpr int   PASS_GRID_N     = 64;
static constexpr float PASS_CELL_SIZE  = CHUNK_SIZE / (float)PASS_GRID_N;

// Forward declaration — TerrainPassGrid_Build receives a NavMesh by const ref.
class NavMesh;

// ── TerrainPassGrid — L2-inspired per-cell walkability bitmask ───────────────
//
// Lineage 2 geodata: per-cell height + 8-directional passability bitmask
// stored in GeoData[MapX]_[MapY]_conv.dat, one file per 1.718km² tile.
//
// MD adaptation: 64×64 grid per 500m chunk → cell = 7.8m.
// 1 bit per cell = walkable(1) / blocked(0). Total: 64 uint64_t = 512 bytes.
// Built once from NavMesh after chunk load via TerrainPassGrid_Build().
//
// Use cases:
//   - O(1) LOS pre-filter in SenseSystem (before expensive Detour raycast)
//   - Melee hit-zone validation (target reachable without solid wall?)
//   - Editor debug overlay (show passability grid as green/red cells)

struct TerrainPassGrid {
    // Row-major bitfield: walkable[row] >> col & 1u == 1 → cell walkable.
    uint64_t walkable[PASS_GRID_N] = {};

    void Clear() noexcept { memset(walkable, 0, sizeof(walkable)); }

    void SetWalkable(int row, int col) noexcept {
        if ((unsigned)row >= (unsigned)PASS_GRID_N) return;
        if ((unsigned)col >= (unsigned)PASS_GRID_N) return;
        walkable[row] |= (uint64_t)1u << col;
    }

    bool IsWalkable(int row, int col) const noexcept {
        if ((unsigned)row >= (unsigned)PASS_GRID_N) return false;
        if ((unsigned)col >= (unsigned)PASS_GRID_N) return false;
        return (walkable[row] >> col) & 1u;
    }

    // Local-coord lookup: lx/lz relative to chunk origin [0 .. 500m).
    bool IsWalkableLocal(float lx, float lz) const noexcept {
        return IsWalkable((int)(lz / PASS_CELL_SIZE), (int)(lx / PASS_CELL_SIZE));
    }
};
static_assert(sizeof(TerrainPassGrid) == PASS_GRID_N * sizeof(uint64_t),
              "TerrainPassGrid: 64 uint64_t = 512 bytes");

// Build: queries NavMesh at each cell centre; marks walkable when Detour finds poly.
// Call after NavMesh::Build(). Returns walkable cell count (0 = navmesh not built).
int  TerrainPassGrid_Build(TerrainPassGrid& grid, const NavMesh& nav,
                            float chunk_origin_x, float chunk_origin_z);

// Bresenham LOS: returns true if every cell on the line is walkable.
// lx0/lz0 and lx1/lz1 are local chunk coords [0..500m).
// Use as O(1) pre-filter before expensive dtNavMeshQuery::raycast.
bool TerrainPassGrid_HasLOS(const TerrainPassGrid& grid,
                              float lx0, float lz0,
                              float lx1, float lz1) noexcept;
