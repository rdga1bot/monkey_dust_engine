#pragma once
#include <cstdint>

namespace md::flare {

// Map dimensions are capped at these values on load.
constexpr int MAX_MAP_WIDTH  = 128;
constexpr int MAX_MAP_HEIGHT = 128;
constexpr int MAX_MAP_LAYERS = 6;
constexpr int MAX_TILESETS   = 8;

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
};

constexpr int MAX_TILE_META = 4096;

struct TileMetaRegistry {
    TileMeta entries[MAX_TILE_META];
    int      count;

    void Clear();
    const TileMeta* Find(uint16_t tile_id) const;
    bool Add(const TileMeta& meta);
    void DumpFirst(int n) const;
};

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
    char  title[64];

    TileSet           tilesets[MAX_TILESETS];
    int               tileset_count;
    TileMetaRegistry  meta;

    TileMapLayer layers[MAX_MAP_LAYERS];
    int          layer_count;
};

// Load Flare .txt or Tiled .tmx map (format detected by file extension).
bool LoadFlareMap(const char* path, FlareMap& out);

// Map a tile_id to the owning tileset and its local index within that tileset.
// Returns false when tile_id == 0 (empty) or no suitable tileset is found.
bool ResolveTile(const FlareMap& map, uint16_t tile_id,
                 int* out_ts_idx, int* out_local_idx);

} // namespace md::flare
