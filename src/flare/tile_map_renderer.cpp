#include <monkey_dust/flare/tile_map_renderer.h>
#include <monkey_dust/render/md_shader.h>

#ifdef MD_OPENGL43_ENABLED
#include "external/glad.h"
#include "raylib.h"   // Matrix, MatrixMultiply
#include <cstring>
#include <cstdio>
#include <cstdlib>    // qsort

// Per-instance GPU layout (stride = 28 bytes):
//   vec2  tile_pos  (grid x, y)   offset  0
//   vec4  uv_rect   (u0,v0,u1,v1) offset  8
//   float tile_h_px (sprite h px) offset 24  (96=flat, >96=billboard)
static constexpr int TINST_STRIDE  = 28;
static constexpr int TINST_OFF_POS =  0;
static constexpr int TINST_OFF_UV  =  8;
static constexpr int TINST_OFF_H   = 24;

// Unit quad corners [0,1]×[0,1] (4 verts, GL_TRIANGLE_FAN).
static const float TILE_QUAD[8] = {
    0.f, 0.f,  1.f, 0.f,  1.f, 1.f,  0.f, 1.f
};

// Painter's algorithm sort scratch.
struct VisibleTile {
    uint16_t col;
    uint16_t row;
    uint16_t tid;
    uint16_t _pad;
    int      local_idx;  // tileset-local UV index, resolved in PASS 1
    int      depth;      // col + row, precomputed sort key
};
static int VisibleTileCmp(const void* a, const void* b) {
    const VisibleTile* va = (const VisibleTile*)a;
    const VisibleTile* vb = (const VisibleTile*)b;
    return va->depth - vb->depth;
}

namespace md::flare {

TileMapRenderer& TileMapRenderer::Get() {
    static TileMapRenderer inst;
    return inst;
}

void TileMapRenderer::Init() {
    if (init_) return;

    glGenBuffers(1, &quad_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(TILE_QUAD), TILE_QUAD, GL_STATIC_DRAW);

    glGenBuffers(1, &inst_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(MAX_VISIBLE_TILES * TINST_STRIDE), nullptr, GL_STREAM_DRAW);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    // Attrib 0: quad corner (vec2, per-vertex)
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glVertexAttribDivisor(0, 0);

    // Attribs 1-2: per-instance data
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, TINST_STRIDE, (void*)TINST_OFF_POS);
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, TINST_STRIDE, (void*)TINST_OFF_UV);
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, TINST_STRIDE, (void*)(intptr_t)TINST_OFF_H);
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);

    MdShader sh = MdLoadShader("shaders/tile_map.vert", "shaders/tile_map.frag");
    prog_ = sh.id;
    if (prog_) {
        loc_vp_        = MdGetLoc(sh, "u_vp");
        loc_tile_size_ = MdGetLoc(sh, "u_tile_size");
        loc_y_         = MdGetLoc(sh, "u_y");
    } else {
        fprintf(stderr, "[TileMap] failed to load tile_map shaders\n");
    }

    init_ = true;
}

void TileMapRenderer::Shutdown() {
    if (!init_) return;
    MdUnloadTexture(atlas_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &quad_vbo_);
    glDeleteBuffers(1, &inst_vbo_);
    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
    init_ = false;
}

void TileMapRenderer::SetAtlas(const char* png_path) {
    MdUnloadTexture(atlas_);
    atlas_ = MdLoadTexturePixelArt(png_path);
    if (!atlas_.id)
        fprintf(stderr, "[TileMap] failed to load atlas: %s\n", png_path);
}

void TileMapRenderer::Render(const FlareMap& map, const MdCamera& cam,
                              float aspect, float tile_world_size, float ortho_size)
{
    if (!init_ || !prog_) return;
    if (map.layer_count == 0 || map.width <= 0 || map.height <= 0) return;

    // Pick background layer (first BACKGROUND, else layer 0).
    const TileMapLayer* bg = nullptr;
    for (int i = 0; i < map.layer_count; ++i)
        if (map.layers[i].type == LayerType::BACKGROUND) { bg = &map.layers[i]; break; }
    if (!bg) bg = &map.layers[0];

    // Detect .txt format: all tilesets have firstgid==0.
    // In that case, tileset[0] is collision and tileset[1] is the first visual set.
    // Tile IDs in .txt background layers are 1-based indices into the visual tileset.
    bool txt_fmt = true;
    for (int i = 0; i < map.tileset_count; ++i)
        if (map.tilesets[i].firstgid > 0) { txt_fmt = false; break; }

    // Visual tileset: index 1 for .txt (skip collision at 0), else firstgid≥2 for TMX.
    int vis_ts_idx = (txt_fmt && map.tileset_count > 1) ? 1 : 0;
    if (!txt_fmt) {
        for (int i = 0; i < map.tileset_count; ++i)
            if (map.tilesets[i].firstgid >= 2) { vis_ts_idx = i; break; }
    }
    const TileSet& vis = map.tilesets[vis_ts_idx];

    // Atlas columns: derive from atlas width if not stored (common for .txt).
    int vis_cols = vis.columns > 0 ? vis.columns :
                   (atlas_.w > 0 && vis.tile_w > 0) ? atlas_.w / vis.tile_w : 16;

    float iaw = (atlas_.w > 0 && vis.tile_w > 0) ? (float)vis.tile_w / atlas_.w : 1.0f / vis_cols;
    float iah = (atlas_.h > 0 && vis.tile_h > 0) ? (float)vis.tile_h / atlas_.h : iaw;

    // Ground diamond UV height.  In Flare .txt tilesets the isometric rhombus
    // always occupies the bottom map.tile_h (=96) px of each slot.  Flat tiles
    // must sample only that strip; billboard tiles use the full slot.
    float ground_iah = (atlas_.h > 0 && map.tile_h > 0)
                       ? (float)map.tile_h / (float)atlas_.h
                       : iah;

    // Total tile slots in atlas — used to skip out-of-range IDs (other tilesets).
    int vis_rows = (atlas_.h > 0 && vis.tile_h > 0) ? atlas_.h / vis.tile_h : 8;
    int vis_max  = vis_cols * vis_rows;

    // Pack instance buffer (painter's algorithm: collect → sort → write).
    static VisibleTile vbuf[MAX_VISIBLE_TILES];
    static uint8_t     ibuf[MAX_VISIBLE_TILES * TINST_STRIDE];
    int n = 0;

    // Collect tiles from one layer into vbuf.
    auto CollectLayer = [&](const TileMapLayer& layer) {
        for (int row = 0; row < map.height && row < MAX_MAP_HEIGHT && n < MAX_VISIBLE_TILES; ++row) {
            for (int col = 0; col < map.width && col < MAX_MAP_WIDTH && n < MAX_VISIBLE_TILES; ++col) {
                uint16_t tid = layer.tiles[row * MAX_MAP_WIDTH + col];
                if (tid == 0) continue;

                int local_idx;
                if (txt_fmt) {
                    local_idx = (int)tid - 1;
                } else {
                    int ts_idx = 0;
                    if (!ResolveTile(map, tid, &ts_idx, &local_idx)) continue;
                    if (map.tilesets[ts_idx].firstgid < 2) continue;
                }
                if (local_idx < 0 || local_idx >= vis_max) continue;

                vbuf[n].col       = (uint16_t)col;
                vbuf[n].row       = (uint16_t)row;
                vbuf[n].tid       = tid;
                vbuf[n].local_idx = local_idx;
                vbuf[n].depth     = col + row;
                ++n;
            }
        }
    };

    // PASS 1: collect background + object layers together.
    // Depth test handles intra-position ordering; painter's sort handles
    // back-to-front order for alpha-tested transparent geometry.
    CollectLayer(*bg);
    for (int i = 0; i < map.layer_count; ++i) {
        if (map.layers[i].type == LayerType::OBJECT)
            CollectLayer(map.layers[i]);
    }

    // PASS 2: sort back-to-front (ascending col+row = painter's order for SE isometric).
    qsort(vbuf, (size_t)n, sizeof(VisibleTile), VisibleTileCmp);

    // PASS 3: write to GPU buffer in sorted order.
    for (int i = 0; i < n; ++i) {
        const VisibleTile& vt = vbuf[i];

        // Resolve UV and sprite height from per-tile metadata when available,
        // falling back to grid-based calculation for unmapped tiles.
        float tile_h_f = 96.0f;
        float u0, v0, u1, v1;

        // UV CONVENTION: MdLoadTexturePixelArt uses stbi vertical flip.
        //   GL v=0 = bottom of image file,  GL v=1 = top of image file.
        //   Correct formula:  v_gl = 1.0f - y_file / atlas_h
        //   Wrong formula:    v_gl = y_file / atlas_h  (produces invisible tiles)
        //
        // CORNER MAPPING:
        //   Flat tile  (h≤96): a_corner.y 0→north(top of sprite)  1→south(bottom)
        //   Billboard  (h>96): a_corner.y 0→ground/base           1→tip/top
        //   For flat:      v0 = 1-(src_y/H),   v1 = 1-((src_y+h)/H)  [v0 > v1]
        //   For billboard: v0 = 1-((src_y+h)/H), v1 = 1-(src_y/H)    [v0 < v1]
        const TileMeta* tm = map.meta.Find(vt.tid);
        if (tm && tm->w > 0 && tm->h > 0 && atlas_.w > 0 && atlas_.h > 0) {
            // Per-tile atlas: stbi flip active → GL v=1−y_file/atlas_h.
            tile_h_f = (float)tm->h;
            if (tile_h_f > 768.0f) tile_h_f = 768.0f;
            u0 = (float)tm->src_x / (float)atlas_.w;
            u1 = (float)(tm->src_x + tm->w) / (float)atlas_.w;
            if (tile_h_f <= 96.5f) {
                // Flat: corner.y=0=north=top of image (low y_file)=high GL v.
                v0 = 1.0f - (float)tm->src_y / (float)atlas_.h;
                v1 = 1.0f - (float)(tm->src_y + tm->h) / (float)atlas_.h;
            } else {
                // Billboard: corner.y=0=ground=base of sprite (src_y+h)=low GL v.
                v0 = 1.0f - (float)(tm->src_y + tm->h) / (float)atlas_.h;
                v1 = 1.0f - (float)tm->src_y / (float)atlas_.h;
            }
        } else {
            // Grid-based fallback (stbi flip active).
            if (tm && tm->h > 0) {
                tile_h_f = (float)tm->h;
                if (tile_h_f > 768.0f) tile_h_f = 768.0f;
            }
            int tc = vt.local_idx % vis_cols;
            int tr = vt.local_idx / vis_cols;
            u0 = tc * iaw;
            u1 = u0 + iaw;
            if (tile_h_f <= 96.5f) {
                // Ground diamond: bottom 96px of each slot.
                // south (corner.y=1) = slot bottom in image = low GL v.
                v1 = 1.0f - (float)(tr + 1) * iah;
                v0 = v1 + ground_iah;
            } else {
                // Billboard: corner.y=0=ground=slot bottom=low GL v.
                v0 = 1.0f - (float)(tr + 1) * iah;
                v1 = 1.0f - (float)tr * iah;
            }
        }

        float tx   = (float)vt.col;
        float ty_f = (float)vt.row;

        uint8_t* p = ibuf + i * TINST_STRIDE;
        memcpy(p + TINST_OFF_POS + 0,  &tx,        4);
        memcpy(p + TINST_OFF_POS + 4,  &ty_f,      4);
        memcpy(p + TINST_OFF_UV  + 0,  &u0,        4);
        memcpy(p + TINST_OFF_UV  + 4,  &v0,        4);
        memcpy(p + TINST_OFF_UV  + 8,  &u1,        4);
        memcpy(p + TINST_OFF_UV  + 12, &v1,        4);
        memcpy(p + TINST_OFF_H,        &tile_h_f,  4);
    }

    if (n == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * TINST_STRIDE), ibuf);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glUseProgram(prog_);

    // Use Raylib's own matrix functions — avoids GLM row/column ordering issues.
    Camera3D rl = cam.ToRaylib();
    Matrix V  = MatrixLookAt(rl.position, rl.target, rl.up);

    Matrix P;
    if (ortho_size > 0.0f) {
        const float oh = ortho_size;
        const float ow = oh * aspect;
        P = MatrixOrtho(-ow, +ow, -oh, +oh, 0.1, 300.0);
    } else {
        P = MatrixPerspective((double)rl.fovy * 0.01745329251844, (double)aspect, 0.1, 300.0);
    }
    Matrix vp = MatrixMultiply(V, P);   // Raylib MM(A,B)=P*V mathematically → correct
    glUniformMatrix4fv(loc_vp_, 1, GL_FALSE, MatrixToFloat(vp));

    float tsz[2] = { tile_world_size, tile_world_size };
    glUniform2fv(loc_tile_size_, 1, tsz);
    glUniform1f (loc_y_, 0.0f);

    if (atlas_.id) MdBindTexture(atlas_, 0);

    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, n);
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    glUseProgram(0);
}

} // namespace md::flare
#endif // MD_OPENGL43_ENABLED
