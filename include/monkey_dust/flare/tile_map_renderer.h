#pragma once
#include <monkey_dust/flare/tile_map.h>
#include <monkey_dust/render/md_camera.h>
#include <monkey_dust/render/md_texture.h>

namespace md::flare {

// Maximum tile instances submitted per Render() call.
// 128×128 map ≈ 16 384 tiles; capped so instance VBO stays fixed-size.
constexpr int MAX_VISIBLE_TILES = 16384;

// Renders one FlareMap background + object layers as instanced geometry.
// One tileset atlas texture shared across all tiles per call.
//
// UV CONVENTION: atlas loaded via MdLoadTexturePixelArt (stbi flip=1).
//   v_gl = 1.0f − y_file/atlas_h  ← MUST match tile_map_renderer.cpp
//   Flat tile  (h≤96): v0=1−src_y/H (north), v1=1−(src_y+h)/H (south)
//   Billboard  (h>96): v0=1−(src_y+h)/H (base), v1=1−src_y/H (tip)
//
// Per-frame usage:
//   SetAtlas(png_path)                  — once per tileset change
//   Render(map, cam, aspect, tile_size) — one instanced draw per frame
class TileMapRenderer {
public:
    static TileMapRenderer& Get();

    void Init();
    void Shutdown();

    // Load the tileset image used as the atlas for subsequent Render() calls.
    void SetAtlas(const char* png_path);

    // Render map background layer.  tile_world_size controls the world-space
    // footprint of one tile (default 1.0 = 1 world unit).
    // ortho_size: orthographic half-height in world units (0 = perspective).
    void Render(const FlareMap& map, const MdCamera& cam,
                float aspect, float tile_world_size = 1.0f,
                float ortho_size = 0.0f);

private:
    TileMapRenderer() = default;

    uint32_t vao_      = 0;
    uint32_t quad_vbo_ = 0;
    uint32_t inst_vbo_ = 0;
    uint32_t prog_     = 0;

    int loc_vp_        = -1;
    int loc_tile_size_ = -1;
    int loc_y_         = -1;

    MdTexture atlas_;

    bool init_ = false;
};

} // namespace md::flare
