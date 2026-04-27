// M7.5 tile map parser test — md::flare::tile_map
// Build:
//   g++ -std=c++17 test_tile_map.cpp ../../src/flare/tile_map.cpp \
//       -I../../include -o test_tile_map && ./test_tile_map

#include <monkey_dust/flare/tile_map.h>
#include <cstdio>
#include <cstring>

using namespace md::flare;

static int g_total = 0;
static int g_pass  = 0;

static void chk(bool ok, const char* label) {
    ++g_total;
    if (ok) { ++g_pass; printf("  PASS  %s\n", label); }
    else                printf("  FAIL  %s\n", label);
}

// FlareMap is ~135 KB — static to keep it off the stack.
static FlareMap gMap;

// ── Flare .txt ────────────────────────────────────────────────────────────────

static void test_txt() {
    printf("\n[txt format]\n");
    memset(&gMap, 0, sizeof(gMap));
    bool ok = LoadFlareMap("fixtures/mini_map.txt", gMap);
    chk(ok,                       "load succeeds");
    chk(gMap.width  == 4,         "width=4");
    chk(gMap.height == 4,         "height=4");
    chk(gMap.tile_w == 192,       "tilewidth=192");
    chk(gMap.tile_h == 96,        "tileheight=96");
    chk(gMap.hero_x == 1.f && gMap.hero_y == 1.f, "hero_pos=1,1");
    chk(strcmp(gMap.music_path, "music/test.ogg") == 0, "music path");
    chk(strcmp(gMap.title, "Mini Test Map") == 0,       "title");
    chk(gMap.tileset_count == 2,  "2 tilesets");
    chk(strcmp(gMap.tilesets[1].image_path, "sheets/ground.png") == 0,
                                  "tileset[1] image path");
    chk(gMap.tilesets[0].tile_w == 192, "tileset[0] tile_w=192");
    chk(gMap.layer_count == 2,    "2 layers");
    chk(gMap.layers[0].type == LayerType::BACKGROUND, "layer[0]=background");
    chk(gMap.layers[1].type == LayerType::COLLISION,  "layer[1]=collision");
    // Background tile (col=1, row=1) = 17
    chk(gMap.layers[0].tiles[1 * MAX_MAP_WIDTH + 1] == 17, "bg[1][1]=17");
    // Background tile (col=0, row=0) = 0
    chk(gMap.layers[0].tiles[0 * MAX_MAP_WIDTH + 0] == 0,  "bg[0][0]=0");
    // Collision tile (col=0, row=0) = 1
    chk(gMap.layers[1].tiles[0 * MAX_MAP_WIDTH + 0] == 1,  "col[0][0]=1");
    // Collision tile (col=1, row=1) = 0
    chk(gMap.layers[1].tiles[1 * MAX_MAP_WIDTH + 1] == 0,  "col[1][1]=0");
}

// ── Tiled .tmx ────────────────────────────────────────────────────────────────

static void test_tmx() {
    printf("\n[tmx format]\n");
    memset(&gMap, 0, sizeof(gMap));
    bool ok = LoadFlareMap("fixtures/mini_map.tmx", gMap);
    chk(ok,                       "load succeeds");
    chk(gMap.width  == 4,         "width=4");
    chk(gMap.height == 4,         "height=4");
    chk(gMap.tile_w == 192,       "tilewidth=192");
    chk(gMap.tile_h == 96,        "tileheight=96");
    chk(gMap.hero_x == 1.f && gMap.hero_y == 1.f, "hero_pos=1,1");
    chk(strcmp(gMap.title, "Mini TMX Map") == 0,   "title");
    chk(gMap.tileset_count == 2,  "2 tilesets");
    chk(gMap.tilesets[0].firstgid == 1,  "tileset[0] firstgid=1");
    chk(gMap.tilesets[1].firstgid == 16, "tileset[1] firstgid=16");
    chk(gMap.tilesets[1].columns  == 16, "tileset[1] columns=16");
    chk(gMap.layer_count == 2,    "2 layers");
    chk(gMap.layers[0].type == LayerType::BACKGROUND, "layer[0]=background");
    chk(gMap.layers[1].type == LayerType::COLLISION,  "layer[1]=collision");
    chk(gMap.layers[0].tiles[1 * MAX_MAP_WIDTH + 1] == 17, "bg[1][1]=17");
    chk(gMap.layers[1].tiles[0 * MAX_MAP_WIDTH + 0] == 1,  "col[0][0]=1");
}

// ── ResolveTile ───────────────────────────────────────────────────────────────

static void test_resolve() {
    printf("\n[ResolveTile — using last loaded tmx map]\n");
    int ts = 0, li = 0;
    chk(!ResolveTile(gMap, 0, &ts, &li),      "tile 0 → not found");
    chk(ResolveTile(gMap, 1, &ts, &li) &&
        ts == 0 && li == 0,                   "tile 1 → ts[0] local=0");
    chk(ResolveTile(gMap, 16, &ts, &li) &&
        ts == 1 && li == 0,                   "tile 16 → ts[1] local=0");
    chk(ResolveTile(gMap, 17, &ts, &li) &&
        ts == 1 && li == 1,                   "tile 17 → ts[1] local=1");
}

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    test_txt();
    test_tmx();
    test_resolve();

    printf("\nTests: %d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
