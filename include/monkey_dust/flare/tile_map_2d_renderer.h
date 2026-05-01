#pragma once
#include <monkey_dust/flare/tile_map.h>
#include <monkey_dust/render/md_texture.h>

namespace md::flare {

// Pixel-perfect 2D Flare renderer for md_flare_demo.
// Uses the exact Flare screen-space formula (SDL_BlitSurface equivalent):
//   sprite_top_left = ((col-row)*96 - offset_x + origin_x,
//                      (col+row)*48 - offset_y + origin_y) * scale
// Tiles are sorted painter's-order (back to front) on the CPU and drawn
// with depth test disabled — no world matrix, no approximation.
class TileMap2DRenderer {
public:
    static TileMap2DRenderer& Get();
    void Init();
    void Shutdown();

    // Load atlas textures from the FlareMap's tileset_atlases paths.
    void SetAtlases(const FlareMap& map);

    // Expose atlas textures so editor tools can render tile thumbnails.
    MdTexture GetAtlas(int idx) const {
        if (idx < 0 || idx >= atlas_count_) return {};
        return atlases_[idx];
    }
    int GetAtlasCount() const { return atlas_count_; }

    // Render the map in 2D screen space.
    //   origin_x, origin_y : screen-pixel position of tile(0,0)'s grid anchor
    //   scale               : zoom factor (1.0 = native atlas pixels)
    //   vp_w, vp_h          : current viewport/window dimensions in pixels
    void Render(const FlareMap& map, float now_s,
                float origin_x, float origin_y, float scale,
                int vp_w, int vp_h);

private:
    bool     init_        = false;
    uint32_t vao_         = 0;
    uint32_t quad_vbo_    = 0;
    uint32_t inst_vbo_    = 0;
    uint32_t prog_        = 0;
    int      loc_viewport_= -1;
    int      loc_atlas_[4]= {-1,-1,-1,-1};

    static constexpr int MAX_ATLAS = 4;
    MdTexture atlases_[MAX_ATLAS]  = {};
    int       atlas_count_         = 0;

    // Per-instance layout (stride 36):
    //   vec2 screen_tl    offset  0
    //   vec2 screen_size  offset  8
    //   vec4 uv_rect      offset 16
    //   float atlas_idx   offset 32
    static constexpr int STRIDE        = 36;
    static constexpr int MAX_TILES     = 16384;
};

} // namespace md::flare
