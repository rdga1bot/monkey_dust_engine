#include <monkey_dust/flare/tile_collision.h>
#include <monkey_dust/nav/nav_system.h>
#include <cstdio>
#include <cmath>

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

// ── Step 1: GetTileHeight ─────────────────────────────────────────────────────

TileHeight GetTileHeight(const TileMeta& meta, float tile_world_size) {
    TileHeight th {};
    if (meta.h == 0) return th;  // empty/sentinel tile → zero height

    // From CLAUDE.md §TINST invariants:
    //   y_top = offset_y / 96 * tsz
    //   y_bot = -(h - offset_y) / 96 * tsz
    //   billboard when offset_y > h/2
    const float inv96 = tile_world_size / 96.f;
    th.y_top   =  (float)meta.offset_y               * inv96;
    th.y_bot   = -((float)(meta.h - meta.offset_y))  * inv96;
    th.is_wall = (meta.offset_y > meta.h / 2);

    // Clamp to valid range.
    if (th.y_top < 0.f) th.y_top = 0.f;
    if (th.y_bot > 0.f) th.y_bot = 0.f;
    return th;
}

// ── Step 2: BuildWorldGeometry ────────────────────────────────────────────────

// World geometry buffers — static, no heap.
// Wall tile:  8 verts, 10 tris  /  Ground/water: 4 verts, 2 tris
// Worst case 128×128 all walls: 131072 verts, 163840 tris
// (GEO_MAX_VERTS / GEO_MAX_TRIS are declared as public constants in tile_collision.h)

// Emit a flat isometric diamond at height y into caller's buffers.
// Returns false if buffers are full.
static bool EmitDiamond(float cx, float y, float cz, float h,
                        float* verts, int max_verts, int& nv,
                        int*   tris,  int max_tris,  int& nt) {
    if (nv + 4 > max_verts || nt + 2 > max_tris) return false;
    float* v = verts + nv * 3;
    v[ 0]=cx;    v[ 1]=y; v[ 2]=cz;       // P0 north
    v[ 3]=cx+h;  v[ 4]=y; v[ 5]=cz+h;    // P1 east
    v[ 6]=cx;    v[ 7]=y; v[ 8]=cz+2*h;  // P2 south
    v[ 9]=cx-h;  v[10]=y; v[11]=cz+h;    // P3 west
    // CCW from +Y → normal up → walkable in Recast
    int* t = tris + nt * 3;
    t[0]=nv; t[1]=nv+3; t[2]=nv+1;
    t[3]=nv+3; t[4]=nv+2; t[5]=nv+1;
    nv += 4; nt += 2;
    return true;
}

// Emit a diamond prism (wall) with top at y_top, bottom at y_bot.
// 8 verts (4 top + 4 bottom), 10 tris (top cap + 4 side quads).
static bool EmitPrism(float cx, float cz, float h,
                      float y_top, float y_bot,
                      float* verts, int max_verts, int& nv,
                      int*   tris,  int max_tris,  int& nt) {
    if (nv + 8 > max_verts || nt + 10 > max_tris) return false;

    // Top 4 verts (indices nv..nv+3)
    float* v = verts + nv * 3;
    v[ 0]=cx;    v[ 1]=y_top; v[ 2]=cz;
    v[ 3]=cx+h;  v[ 4]=y_top; v[ 5]=cz+h;
    v[ 6]=cx;    v[ 7]=y_top; v[ 8]=cz+2*h;
    v[ 9]=cx-h;  v[10]=y_top; v[11]=cz+h;
    // Bottom 4 verts (indices nv+4..nv+7)
    v[12]=cx;    v[13]=y_bot; v[14]=cz;
    v[15]=cx+h;  v[16]=y_bot; v[17]=cz+h;
    v[18]=cx;    v[19]=y_bot; v[20]=cz+2*h;
    v[21]=cx-h;  v[22]=y_bot; v[23]=cz+h;

    int b = nv;   // base index
    int* t = tris + nt * 3;
    // Top cap (CCW from +Y)
    t[ 0]=b;   t[ 1]=b+3; t[ 2]=b+1;
    t[ 3]=b+3; t[ 4]=b+2; t[ 5]=b+1;
    // 4 side quads — CCW from outside
    // North face: P0top P1top P1bot P0bot
    t[ 6]=b;   t[ 7]=b+1; t[ 8]=b+5;
    t[ 9]=b;   t[10]=b+5; t[11]=b+4;
    // East face: P1top P2top P2bot P1bot
    t[12]=b+1; t[13]=b+2; t[14]=b+6;
    t[15]=b+1; t[16]=b+6; t[17]=b+5;
    // South face: P2top P3top P3bot P2bot
    t[18]=b+2; t[19]=b+3; t[20]=b+7;
    t[21]=b+2; t[22]=b+7; t[23]=b+6;
    // West face: P3top P0top P0bot P3bot
    t[24]=b+3; t[25]=b;   t[26]=b+4;
    t[27]=b+3; t[28]=b+4; t[29]=b+7;

    nv += 8; nt += 10;
    return true;
}

bool BuildWorldGeometry(const FlareMap& map, float tile_world_size,
                        float* out_verts, int max_verts, int& out_nv,
                        int*   out_tris,  int max_tris,  int& out_nt) {
    out_nv = 0; out_nt = 0;
    if (map.width <= 0 || map.height <= 0) return false;

    const TileMapLayer* col_l = FindLayer(map, LayerType::COLLISION);
    const TileMapLayer* bg_l  = FindLayer(map, LayerType::BACKGROUND);
    const float h             = tile_world_size * 0.5f;
    const float water_y       = -0.5f * tile_world_size;

    // Default wall height when TileMeta is unavailable.
    const float default_wall_top = 0.f;
    const float default_wall_bot = -tile_world_size;

    for (int row = 0; row < map.height && row < MAX_MAP_HEIGHT; ++row) {
        for (int col = 0; col < map.width && col < MAX_MAP_WIDTH; ++col) {
            int cid = col_l ? (int)col_l->tiles[row * MAX_MAP_WIDTH + col] : 0;

            float cx = ((float)col - (float)row) * h;
            float cz = ((float)col + (float)row) * h;

            if (cid == 0) {
                // Ground — flat diamond at y=0.
                if (!EmitDiamond(cx, 0.f, cz, h,
                                 out_verts, max_verts, out_nv,
                                 out_tris,  max_tris,  out_nt))
                    return false;

            } else if (cid == 1) {
                // Wall/cliff — diamond prism; height from background tile TileMeta.
                float y_top = default_wall_top;
                float y_bot = default_wall_bot;

                if (bg_l) {
                    uint16_t tid = bg_l->tiles[row * MAX_MAP_WIDTH + col];
                    const TileMeta* m = map.meta.Find(tid);
                    if (m && m->h > 0) {
                        TileHeight th = GetTileHeight(*m, tile_world_size);
                        y_top = th.y_top;
                        y_bot = th.y_bot;
                        // Ensure minimum wall thickness.
                        if (y_top - y_bot < 0.05f * tile_world_size) {
                            y_top = 0.f;
                            y_bot = default_wall_bot;
                        }
                    }
                }
                if (!EmitPrism(cx, cz, h, y_top, y_bot,
                               out_verts, max_verts, out_nv,
                               out_tris,  max_tris,  out_nt))
                    return false;

            } else if (cid == 2) {
                // Water — flat diamond, depressed below ground.
                if (!EmitDiamond(cx, water_y, cz, h,
                                 out_verts, max_verts, out_nv,
                                 out_tris,  max_tris,  out_nt))
                    return false;
            }
            // cid == 3 (void) → skip
        }
    }

    fprintf(stdout, "[WorldGeo] %dx%d map → %d verts, %d tris\n",
            map.width, map.height, out_nv, out_nt);
    return out_nt > 0;
}

// ── Step 3: ExportWorldOBJ ────────────────────────────────────────────────────

bool ExportWorldOBJ(const FlareMap& map, float tile_world_size,
                    const char* out_path) {
    // Build geometry into static buffers.
    static float s_geo_verts[GEO_MAX_VERTS * 3];
    static int   s_geo_tris [GEO_MAX_TRIS  * 3];
    int nv = 0, nt = 0;

    if (!BuildWorldGeometry(map, tile_world_size,
                            s_geo_verts, GEO_MAX_VERTS, nv,
                            s_geo_tris,  GEO_MAX_TRIS,  nt))
        return false;

    FILE* f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "[WorldGeo] cannot open '%s' for writing\n", out_path);
        return false;
    }

    fprintf(f, "# monkey_dust world geometry export\n");
    fprintf(f, "# map: %s  (%dx%d tiles)\n", map.title, map.width, map.height);
    fprintf(f, "# tile_world_size: %.4f\n\n", tile_world_size);

    // Vertices
    for (int i = 0; i < nv; ++i) {
        const float* v = s_geo_verts + i * 3;
        fprintf(f, "v %.6f %.6f %.6f\n", v[0], v[1], v[2]);
    }

    fprintf(f, "\n");

    // Faces (OBJ is 1-indexed)
    for (int i = 0; i < nt; ++i) {
        const int* t = s_geo_tris + i * 3;
        fprintf(f, "f %d %d %d\n", t[0]+1, t[1]+1, t[2]+1);
    }

    fclose(f);
    fprintf(stdout, "[WorldGeo] OBJ written → %s  (%d verts, %d tris)\n",
            out_path, nv, nt);
    return true;
}

} // namespace md::flare
