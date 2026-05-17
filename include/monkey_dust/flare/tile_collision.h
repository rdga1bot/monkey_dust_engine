#pragma once
#include <monkey_dust/flare/tile_map.h>

namespace md::flare {

// Maximum geometry buffer sizes for BuildWorldGeometry / ExportWorldOBJ.
// Sized for a full 128×128 map where every tile is a wall (worst case).
static constexpr int GEO_MAX_VERTS = 131072;   // 128*128*8 verts/wall
static constexpr int GEO_MAX_TRIS  = 163840;   // 128*128*10 tris/wall

// ── Step 1: height inference from sprite metadata ─────────────────────────────
//
// World-space vertical extents for one tile, derived from TileMeta dimensions.
// Matches TileMap2DRenderer's billboard/flat classification (CLAUDE.md §TINST):
//
//   is_wall = (offset_y > h / 2)              ← billboard sprite → vertical object
//   y_top   =  offset_y        / 96.f * tsz   ← how far above ground (≥ 0)
//   y_bot   = -(h - offset_y) / 96.f * tsz   ← how far below ground (≤ 0)
//
// Flat sentinel: h == 0 or offset_y == 0 → y_top = y_bot = 0.
struct TileHeight {
    float y_bot;   // base of tile geometry in world-Y (≤ 0)
    float y_top;   // top  of tile geometry in world-Y (≥ 0)
    bool  is_wall; // true → tile has vertical extent (cliff, tree, wall)
};

// Compute TileHeight from a TileMeta entry.
// tile_world_size must match the value used in Render / BuildNavMeshFromTileMap.
TileHeight GetTileHeight(const TileMeta& meta, float tile_world_size = 1.0f);

// ── Step 2: full 3D world geometry ───────────────────────────────────────────
//
// Builds a triangle soup for the entire map in three layers:
//   Ground  (collision == 0): flat isometric diamonds at y = 0
//   Walls   (collision == 1): diamond prisms extruded from y_bot to y_top
//                             (height inferred from BACKGROUND layer TileMeta)
//   Water   (collision == 2): flat isometric diamonds at y = -0.5 * tile_world_size
//   Void    (collision == 3): skipped
//
// out_verts : float[3] per vertex  (x, y, z)     — caller supplies buffer
// out_tris  : int[3]   per triangle (v0, v1, v2)  — caller supplies buffer
// out_nv / out_nt : written vertex / triangle counts on success
//
// Returns false if the buffers are too small or the map is empty.
// Typical worst-case sizes for a 128×128 map:
//   verts  ≤ 128*128*8 = 131072   (8 verts/wall tile)
//   tris   ≤ 128*128*10 = 163840  (10 tris/wall tile)
bool BuildWorldGeometry(const FlareMap& map, float tile_world_size,
                        float* out_verts, int max_verts, int& out_nv,
                        int*   out_tris,  int max_tris,  int& out_nt);

// ── Step 3: OBJ export ────────────────────────────────────────────────────────
//
// Writes the full 3D world geometry to a Wavefront OBJ file.
// Open with Blender (File → Import → Wavefront OBJ) to inspect the 3D world.
// Returns false on I/O error or empty geometry.
bool ExportWorldOBJ(const FlareMap& map, float tile_world_size,
                    const char* out_path);

// ── NavMesh (existing) ────────────────────────────────────────────────────────

// Build a NavMesh for NavSystem from a FlareMap's COLLISION layer.
// Returns false when the collision layer is absent, the map is empty, or
// Recast/Detour fails to build the NavMesh.
bool BuildNavMeshFromTileMap(const FlareMap& map,
                             float tile_world_size = 1.0f);

// Returns the number of walkable tiles (collision == 0).
int CountWalkableTiles(const FlareMap& map);

} // namespace md::flare
