#pragma once
// Flare map types and loader.
//
// UV CONVENTION (affects anyone computing tile UVs):
//   MdLoadTexturePixelArt uses stbi_set_flip_vertically_on_load(1).
//   → GL v=0 = bottom of image file, v=1 = top.
//   → Correct UV formula: v_gl = 1.0f - y_file / atlas_h
//   See: tile_map_renderer.cpp, CLAUDE_ARCH.md §"Flare Tileset UV Convention"
//
// TILESETDEF img= / atlas_idx RULE (M7.23):
//   A tilesetdef has multiple [tileset] sections, each starting with img=.
//   Each section owns the tile= entries that follow it until the next [tileset].
//   ParseTilesetDefFile() assigns TileMeta::atlas_idx = section index (0-based).
//   TileMapRenderer::SetAtlases() loads all atlas textures; Render() switches
//   textures between draw calls to maintain painter's depth order.
#include <cstdint>

namespace md::flare {

// Map dimensions are capped at these values on load.
constexpr int MAX_MAP_WIDTH  = 128;
constexpr int MAX_MAP_HEIGHT = 128;
constexpr int MAX_MAP_LAYERS = 6;
constexpr int MAX_TILESETS   = 8;
// Maximum distinct atlases a tilesetdef may reference.
// goblin_camp uses 2 (grassland + water); other maps may use up to 4.
constexpr int MAX_ATLAS_COUNT = 4;

// FlareMap structs total ~135 KB — declare as static or heap-allocate; do not
// put on the call stack without thought.

enum class LayerType : uint8_t {
    BACKGROUND,
    FRINGE,
    OBJECT,
    COLLISION,
    UNKNOWN,
};

struct TileSet {
    char image_path[128];
    int  tile_w;      // pixel width of one tile
    int  tile_h;      // pixel height of one tile
    int  offset_x;
    int  offset_y;
    int  columns;     // tiles per row in the sheet (0 = unknown)
    int  firstgid;    // TMX: base tile ID (1-based). .txt: 0 (unknown).
};

// Per-tile metadata from tilesetdef. One entry per tile= line.
struct TileMeta {
    uint16_t tile_id;
    int16_t  src_x, src_y;   // upper-left in atlas (px)
    int16_t  w, h;            // tile size in atlas (px) — h > 96 means billboard
    int16_t  offset_x, offset_y;
    uint8_t  atlas_idx;       // index into TileMetaRegistry::atlas_paths[] (0-based)
    uint8_t  _pad2;
};

constexpr int MAX_TILE_META   = 4096;
constexpr int MAX_ANIM_FRAMES = 8;   // frames per animated tile (max observed: 4)
constexpr int MAX_ANIM_TILES  = 64;  // animated tiles per tilesetdef (observed: 57)

// One frame of a tile animation.  src_x/src_y override the base TileMeta
// coordinates for this frame; w/h/atlas_idx stay the same as the base tile.
struct TileAnimFrame {
    int16_t  src_x, src_y;   // atlas upper-left for this frame (px)
    uint32_t duration_ms;     // how long this frame is displayed
};

// Animation sequence for one tile ID.  Stored in TileMetaRegistry.
// At render time: pick frame where (now_ms % total_ms) falls in its window.
struct TileAnim {
    uint16_t      tile_id;
    uint8_t       frame_count;
    uint8_t       atlas_idx;              // matches TileMeta::atlas_idx
    TileAnimFrame frames[MAX_ANIM_FRAMES];
    uint32_t      total_ms;              // sum of all frame durations
};

struct TileMetaRegistry {
    TileMeta entries[MAX_TILE_META];
    int      count;

    // Primary atlas relative path (first img= line) — backward compat with M7.22.
    // Use atlas_paths[0] for new code; atlas_rel_path always equals atlas_paths[0].
    char     atlas_rel_path[256];

    // All atlas relative paths in tilesetdef section order.
    // atlas_paths[i] corresponds to TileMeta::atlas_idx == i.
    char     atlas_paths[MAX_ATLAS_COUNT][256];
    int      atlas_count;

    TileAnim anim_entries[MAX_ANIM_TILES];
    int      anim_count;

    void Clear();
    const TileMeta* Find(uint16_t tile_id) const;
    bool Add(const TileMeta& m);
    bool AddAnim(const TileAnim& anim);
    const TileAnim* FindAnim(uint16_t tile_id) const;
    void DumpFirst(int n) const;
};

// One [enemy] block parsed from map .txt.
struct FlareSpawn {
    char  category[32];   // e.g. "goblin", "minotaur"
    float center_x;       // tile coord (col)
    float center_y;       // tile coord (row)
    int   number_min;     // spawn count (min of range)
    int   level;
    int   wander_radius;
};

constexpr int MAX_SPAWNS_PER_MAP = 256;

// Flat row-major tile array.  Index: tiles[row * MAX_MAP_WIDTH + col].
// Value 0 = empty cell.
struct TileMapLayer {
    LayerType type;
    uint16_t  tiles[MAX_MAP_WIDTH * MAX_MAP_HEIGHT];
};

struct FlareMap {
    int   width, height;      // tile dimensions of the map
    int   tile_w, tile_h;     // pixel size of one tile
    float hero_x, hero_y;     // spawn position in tile coords
    char  music_path[128];
    char  tileset_def[128];   // "tilesetdefs/tileset_grassland.txt"
    char  tileset_atlas[256]; // resolved absolute path of atlas[0] (backward compat)
    // All resolved atlas absolute paths (parallel to meta.atlas_paths[]).
    // Indexed by TileMeta::atlas_idx.  Pass to TileMapRenderer::SetAtlases().
    char  tileset_atlases[MAX_ATLAS_COUNT][256];
    int   tileset_atlas_count;
    char  title[64];

    TileSet           tilesets[MAX_TILESETS];
    int               tileset_count;
    TileMetaRegistry  meta;

    TileMapLayer layers[MAX_MAP_LAYERS];
    int          layer_count;

    FlareSpawn   spawns[MAX_SPAWNS_PER_MAP];
    int          spawn_count;
};

// Load Flare .txt or Tiled .tmx map (format detected by file extension).
bool LoadFlareMap(const char* path, FlareMap& out);

// Serialize map back to Flare .txt format.
// Skips layers with type == UNKNOWN. Enemy spawns become [enemy] blocks.
// Returns false on I/O error.
bool SaveFlareMap(const char* path, const FlareMap& map);

// Map a tile_id to the owning tileset and its local index within that tileset.
// Returns false when tile_id == 0 (empty) or no suitable tileset is found.
bool ResolveTile(const FlareMap& map, uint16_t tile_id,
                 int* out_ts_idx, int* out_local_idx);

} // namespace md::flare
