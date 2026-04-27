#include <monkey_dust/flare/tile_collision.h>
#include <monkey_dust/nav/nav_system.h>
#include <cstdio>

namespace md::flare {

// ── Geometry buffers ──────────────────────────────────────────────────────────
// Static to avoid heap allocation at load time.
// Sized for the maximum possible walkable tile count (128×128 map, 4 verts each).

static constexpr int WALK_MAX_TILES = MAX_MAP_WIDTH * MAX_MAP_HEIGHT;
static constexpr int WALK_MAX_VERTS = WALK_MAX_TILES * 4;
static constexpr int WALK_MAX_TRIS  = WALK_MAX_TILES * 2;

static float s_verts[WALK_MAX_VERTS * 3];
static int   s_tris [WALK_MAX_TRIS  * 3];

// ── Helpers ───────────────────────────────────────────────────────────────────

static const TileMapLayer* FindLayer(const FlareMap& map, LayerType t) {
    for (int i = 0; i < map.layer_count; ++i)
        if (map.layers[i].type == t) return &map.layers[i];
    return nullptr;
}

static bool IsTileWalkable(const TileMapLayer* col_layer, int row, int col) {
    if (!col_layer) return true;
    uint16_t cid = col_layer->tiles[row * MAX_MAP_WIDTH + col];
    return cid == 0;
}

// ── CountWalkableTiles ────────────────────────────────────────────────────────

int CountWalkableTiles(const FlareMap& map) {
    const TileMapLayer* col = FindLayer(map, LayerType::COLLISION);
    int count = 0;
    for (int row = 0; row < map.height && row < MAX_MAP_HEIGHT; ++row)
        for (int c = 0; c < map.width && c < MAX_MAP_WIDTH; ++c)
            if (IsTileWalkable(col, row, c)) ++count;
    return count;
}

// ── BuildNavMeshFromTileMap ───────────────────────────────────────────────────
//
// Each walkable tile contributes a DIAMOND (rhombus) polygon matching the
// isometric grid layout used by TileMapRenderer.
//
// Tile (col, row) in isometric world XZ:
//   P0 = (cx,    0, cz   )   ← north corner  (grid point col,   row  )
//   P1 = (cx+h,  0, cz+h )   ← east  corner  (grid point col+1, row  )
//   P2 = (cx,    0, cz+2h)   ← south corner  (grid point col+1, row+1)
//   P3 = (cx-h,  0, cz+h )   ← west  corner  (grid point col,   row+1)
//   where cx=(col-row)*h, cz=(col+row)*h, h=tile_world_size/2
//
// Adjacent tiles share diamond corners → seamless tiling, no gaps or overlaps.
//
// Triangle winding is CCW viewed from +Y so Recast marks them walkable
// (normals point upward).

bool BuildNavMeshFromTileMap(const FlareMap& map, float tile_world_size) {
    if (map.width <= 0 || map.height <= 0) return false;

    const TileMapLayer* col = FindLayer(map, LayerType::COLLISION);

    int nv = 0;
    int nt = 0;
    const float h = tile_world_size * 0.5f;

    for (int row = 0; row < map.height && row < MAX_MAP_HEIGHT; ++row) {
        for (int col_idx = 0; col_idx < map.width && col_idx < MAX_MAP_WIDTH; ++col_idx) {
            if (!IsTileWalkable(col, row, col_idx)) continue;
            if (nv + 4 > WALK_MAX_VERTS || nt + 2 > WALK_MAX_TRIS) {
                fprintf(stderr, "[TileCollision] geometry buffer full at tile (%d,%d)\n",
                        col_idx, row);
                goto submit;
            }

            // Isometric diamond center for grid cell origin P0
            float cx = ((float)col_idx - (float)row) * h;
            float cz = ((float)col_idx + (float)row) * h;

            float* v = s_verts + nv * 3;
            v[ 0] = cx;      v[ 1] = 0.0f; v[ 2] = cz;        // P0 north
            v[ 3] = cx + h;  v[ 4] = 0.0f; v[ 5] = cz + h;    // P1 east
            v[ 6] = cx;      v[ 7] = 0.0f; v[ 8] = cz + 2*h;  // P2 south
            v[ 9] = cx - h;  v[10] = 0.0f; v[11] = cz + h;    // P3 west

            // CCW from +Y → normals = +Y → rcMarkWalkableTriangles marks walkable
            int* t = s_tris + nt * 3;
            t[0] = nv;     t[1] = nv + 3; t[2] = nv + 1;  // P0-P3-P1
            t[3] = nv + 3; t[4] = nv + 2; t[5] = nv + 1;  // P3-P2-P1

            nv += 4;
            nt += 2;
        }
    }

submit:
    if (nv == 0) {
        fprintf(stderr, "[TileCollision] no walkable tiles found\n");
        return false;
    }

    fprintf(stdout, "[TileCollision] map %dx%d → %d walkable tiles → %d verts %d tris\n",
            map.width, map.height, nt / 2, nv, nt);

    // cs = half tile size → 2 voxels per tile edge; ch thin for flat terrain.
    // Small minRegionArea to preserve narrow corridors (1-tile-wide paths).
    return NavSystem::Get().BuildForTileMap(s_verts, nv, s_tris, nt,
                                            h,      // cs = half tile size
                                            0.1f);  // ch = flat terrain
}

} // namespace md::flare
