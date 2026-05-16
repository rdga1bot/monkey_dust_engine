#pragma once
#include <monkey_dust/flare/tile_map.h>
#include <monkey_dust/render/md_texture.h>

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_hal.h>
#include <SDL3/SDL_gpu.h>
#endif

namespace md::flare {

// Pixel-perfect 2D Flare renderer for md_flare_demo.
// Uses the exact Flare screen-space formula (SDL_BlitSurface equivalent):
//   sprite_top_left = ((col-row)*96 - offset_x + origin_x,
//                      (col+row)*48 - offset_y + origin_y) * scale
// Tiles are sorted painter's-order (back to front) on the CPU and drawn
// with depth test disabled — no world matrix, no approximation.
//
// Backends:
//   MD_OPENGL43_ENABLED: instanced draw (stride=36, TRIANGLE_FAN, 4 verts/tile).
//   MD_SDL_GPU:          flat vertex buffer (stride=44, TRIANGLES, 6 verts/tile).
//     When GpuDevice::IsReady(), the SDL_GPU path is selected at Init() time.
class TileMap2DRenderer {
public:
    static TileMap2DRenderer& Get();
    void Init();
    void Shutdown();

    // Load atlas textures from the FlareMap's tileset_atlases paths.
    void SetAtlases(const FlareMap& map);

    // Expose atlas textures so editor tools can render tile thumbnails.
    // Non-inline: member offsets must be resolved in the engine TU (MD_SDL_GPU
    // members before atlases_[] make the layout differ between engine and editor).
    MdTexture GetAtlas(int idx) const;
    int       GetAtlasCount()   const;

    // Render the map in 2D screen space.
    //   origin_x, origin_y : screen-pixel position of tile(0,0)'s grid anchor
    //   scale               : zoom factor (1.0 = native atlas pixels)
    //   vp_w, vp_h          : current viewport/window dimensions in pixels
    // layer_mask: bit i = 1 means layer i is visible (default 0xFF = all visible).
    //
    // SDL_GPU path: acquires command buffer + swapchain texture, renders, submits.
    void Render(const FlareMap& map, float now_s,
                float origin_x, float origin_y, float scale,
                int vp_w, int vp_h, uint8_t layer_mask = 0xFF);

    // Load a sprite sheet for NPC overlay rendering.
    // Must be called after SetAtlases(). Uses atlas binding slot atlas_count_
    // (first unused slot). Call once after map load.
    void SetNpcSpriteSheet(const char* path);

    // Set NPC sprites for overlay rendering on next Render().
    //   tile_x/z  — tile-space positions
    //   rot_y     — facing angle (atan2(dx,dz) from movement direction)
    //   is_moving — non-zero → run animation; 0 → stance
    //   count     — clamped to MAX_NPC_DOTS
    //   now_s     — current time for animation cycling
    static constexpr int MAX_NPC_DOTS = 64;
    void SetNpcSprites(const float* tile_x, const float* tile_z,
                       const float* rot_y,  const uint8_t* is_moving,
                       int count, float now_s);

private:
    bool init_ = false;

    // ── OpenGL path (MD_OPENGL43_ENABLED) ────────────────────────────────────
    uint32_t vao_          = 0;
    uint32_t quad_vbo_     = 0;
    uint32_t inst_vbo_     = 0;
    uint32_t prog_         = 0;
    int      loc_viewport_ = -1;
    int      loc_atlas_[4] = {-1,-1,-1,-1};

#ifdef MD_SDL_GPU
    // ── SDL_GPU path ─────────────────────────────────────────────────────────
    bool            sdl_init_          = false;
    GpuPipeline     sdl_pipeline_;
    GpuVertexBuffer sdl_vbuf_;
    void*           sdl_dummy_tex_     = nullptr;  // SDL_GPUTexture* — fallback for unused atlas slots
    void*           sdl_dummy_sampler_ = nullptr;  // SDL_GPUSampler*

    // SDL_GPU vertex layout (stride=44, TRIANGLES, 6 verts/tile, no instancing):
    //   offset  0: a_corner      vec2  (8 bytes)  — (0/1, 0/1) corner of the quad
    //   offset  8: a_screen_tl   vec2  (8 bytes)  — screen-pixel top-left
    //   offset 16: a_screen_size vec2  (8 bytes)  — screen-pixel width/height
    //   offset 24: a_uv_rect     vec4  (16 bytes) — (u0,v0,u1,v1) in atlas coords
    //   offset 40: a_atlas_idx   float (4 bytes)  — 0..3 atlas selector
    static constexpr int STRIDE_SDL = 44;

    void RenderSDLGPU(const FlareMap& map, float now_s,
                      float origin_x, float origin_y, float scale,
                      int vp_w, int vp_h, uint8_t layer_mask,
                      SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swap_tex);
#endif // MD_SDL_GPU

    static constexpr int MAX_ATLAS  = 4;
    MdTexture atlases_[MAX_ATLAS]   = {};
    int       atlas_count_          = 0;

    // NPC sprite sheet (atlas slot = npc_atlas_slot_; -1 = none loaded).
    MdTexture npc_sprite_tex_;
    int       npc_atlas_slot_       = -1;
    // goblin.png sheet dimensions (fantasycore).
    static constexpr int NPC_SHEET_W=1316, NPC_SHEET_H=3185;

    float   npc_dot_x_[MAX_NPC_DOTS]  = {};
    float   npc_dot_z_[MAX_NPC_DOTS]  = {};
    float   npc_rot_y_[MAX_NPC_DOTS]  = {};
    uint8_t npc_moving_[MAX_NPC_DOTS] = {};
    int     npc_dot_count_            = 0;
    float   npc_now_s_                = 0.f;

    // OpenGL instance buffer layout (stride=36, 1 entry/tile):
    static constexpr int STRIDE     = 36;
    static constexpr int MAX_TILES  = 16384;
};

} // namespace md::flare
