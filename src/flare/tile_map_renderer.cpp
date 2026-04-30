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
    uint8_t  atlas_idx;  // TileMeta::atlas_idx — which atlas texture to sample
    uint8_t  _pad;
    int      local_idx;  // tileset-local UV index (grid fallback only)
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
    for (int i = 0; i < atlas_count_; ++i) MdUnloadTexture(atlases_[i]);
    atlas_count_ = 0;
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &quad_vbo_);
    glDeleteBuffers(1, &inst_vbo_);
    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
    init_ = false;
}

void TileMapRenderer::SetAtlases(const FlareMap& map) {
    for (int i = 0; i < atlas_count_; ++i) MdUnloadTexture(atlases_[i]);
    atlas_count_ = 0;
    for (int i = 0; i < map.tileset_atlas_count && i < MAX_ATLAS_COUNT; ++i) {
        if (!map.tileset_atlases[i][0]) { ++atlas_count_; continue; }
        atlases_[atlas_count_] = MdLoadTexturePixelArt(map.tileset_atlases[i]);
        if (!atlases_[atlas_count_].id)
            fprintf(stderr, "[TileMap] atlas[%d] failed: %s\n", i, map.tileset_atlases[i]);
        ++atlas_count_;
    }
}

void TileMapRenderer::SetAtlas(const char* png_path) {
    for (int i = 0; i < atlas_count_; ++i) MdUnloadTexture(atlases_[i]);
    atlas_count_ = 1;
    atlases_[0] = MdLoadTexturePixelArt(png_path);
    if (!atlases_[0].id)
        fprintf(stderr, "[TileMap] failed to load atlas: %s\n", png_path);
}

void TileMapRenderer::Render(const FlareMap& map, const MdCamera& cam,
                              float aspect, float tile_world_size, float ortho_size)
{
    if (!init_ || !prog_ || atlas_count_ == 0) return;
    if (map.layer_count == 0 || map.width <= 0 || map.height <= 0) return;

    // Pick background layer (first BACKGROUND, else layer 0).
    const TileMapLayer* bg = nullptr;
    for (int i = 0; i < map.layer_count; ++i)
        if (map.layers[i].type == LayerType::BACKGROUND) { bg = &map.layers[i]; break; }
    if (!bg) bg = &map.layers[0];

    // Detect .txt format: all tilesets have firstgid==0.
    bool txt_fmt = true;
    for (int i = 0; i < map.tileset_count; ++i)
        if (map.tilesets[i].firstgid > 0) { txt_fmt = false; break; }

    // Visual tileset for grid-based UV fallback (atlas[0] only).
    int vis_ts_idx = (txt_fmt && map.tileset_count > 1) ? 1 : 0;
    if (!txt_fmt) {
        for (int i = 0; i < map.tileset_count; ++i)
            if (map.tilesets[i].firstgid >= 2) { vis_ts_idx = i; break; }
    }
    const TileSet& vis  = map.tilesets[vis_ts_idx];
    const MdTexture& a0 = atlases_[0];

    int vis_cols = vis.columns > 0 ? vis.columns :
                   (a0.w > 0 && vis.tile_w > 0) ? a0.w / vis.tile_w : 16;
    float iaw = (a0.w > 0 && vis.tile_w > 0) ? (float)vis.tile_w / a0.w : 1.0f / vis_cols;
    float iah = (a0.h > 0 && vis.tile_h > 0) ? (float)vis.tile_h / a0.h : iaw;
    float ground_iah = (a0.h > 0 && map.tile_h > 0) ? (float)map.tile_h / (float)a0.h : iah;
    int vis_rows = (a0.h > 0 && vis.tile_h > 0) ? a0.h / vis.tile_h : 8;
    int vis_max  = vis_cols * vis_rows;

    static VisibleTile vbuf[MAX_VISIBLE_TILES];
    static uint8_t     ibuf[MAX_VISIBLE_TILES * TINST_STRIDE];
    int n = 0;

    // PASS 1: collect background + object layers into vbuf.
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

                // atlas_idx from metadata; grid fallback only for atlas[0] tiles.
                const TileMeta* tm_c = map.meta.Find(tid);
                uint8_t aidx = tm_c ? tm_c->atlas_idx : 0;
                // Skip tiles with no metadata that fall outside atlas[0] grid.
                if (!tm_c && (local_idx < 0 || local_idx >= vis_max)) continue;

                vbuf[n].col       = (uint16_t)col;
                vbuf[n].row       = (uint16_t)row;
                vbuf[n].tid       = tid;
                vbuf[n].atlas_idx = aidx;
                vbuf[n].local_idx = local_idx;
                vbuf[n].depth     = col + row;
                ++n;
            }
        }
    };

    CollectLayer(*bg);
    for (int i = 0; i < map.layer_count; ++i)
        if (map.layers[i].type == LayerType::OBJECT)
            CollectLayer(map.layers[i]);

    // PASS 2: sort back-to-front (ascending col+row = painter's order).
    qsort(vbuf, (size_t)n, sizeof(VisibleTile), VisibleTileCmp);

    // PASS 3: compute UV for every tile into ibuf (full sorted buffer).
    //
    // UV CONVENTION: MdLoadTexturePixelArt uses stbi vertical flip.
    //   GL v=0 = bottom of image file,  GL v=1 = top of image file.
    //   Correct formula:  v_gl = 1.0f - y_file / atlas_h
    //   Wrong formula:    v_gl = y_file / atlas_h  (produces invisible tiles)
    //
    // CORNER MAPPING:
    //   Flat tile  (h≤96): a_corner.y 0→north(top of sprite)  1→south(bottom)
    //   Billboard  (h>96): a_corner.y 0→ground/base           1→tip/top
    //   For flat:      v0 = 1-(src_y/H),    v1 = 1-((src_y+h)/H)  [v0 > v1]
    //   For billboard: v0 = 1-((src_y+h)/H), v1 = 1-(src_y/H)    [v0 < v1]
    for (int i = 0; i < n; ++i) {
        const VisibleTile& vt = vbuf[i];
        float tile_h_f = 96.0f;
        float u0, v0, u1, v1;

        const TileMeta* tm = map.meta.Find(vt.tid);
        // Per-tile metadata path: use exact pixel coordinates + owning atlas dims.
        if (tm && tm->w > 0 && tm->h > 0) {
            uint8_t aidx = tm->atlas_idx < (uint8_t)atlas_count_ ? tm->atlas_idx : 0;
            const MdTexture& atl = atlases_[aidx];
            if (atl.w > 0 && atl.h > 0) {
                tile_h_f = (float)tm->h;
                if (tile_h_f > 768.0f) tile_h_f = 768.0f;
                u0 = (float)tm->src_x / (float)atl.w;
                u1 = (float)(tm->src_x + tm->w) / (float)atl.w;
                if (tile_h_f <= 96.5f) {
                    v0 = 1.0f - (float)tm->src_y / (float)atl.h;
                    v1 = 1.0f - (float)(tm->src_y + tm->h) / (float)atl.h;
                } else {
                    v0 = 1.0f - (float)(tm->src_y + tm->h) / (float)atl.h;
                    v1 = 1.0f - (float)tm->src_y / (float)atl.h;
                }
            } else {
                u0 = 0.0f; u1 = 1.0f; v0 = 1.0f; v1 = 0.0f;
            }
        } else {
            // Grid fallback (atlas[0] only — tiles without metadata).
            if (tm && tm->h > 0) {
                tile_h_f = (float)tm->h;
                if (tile_h_f > 768.0f) tile_h_f = 768.0f;
            }
            int tc = vt.local_idx % vis_cols;
            int tr = vt.local_idx / vis_cols;
            u0 = tc * iaw;
            u1 = u0 + iaw;
            if (tile_h_f <= 96.5f) {
                v1 = 1.0f - (float)(tr + 1) * iah;
                v0 = v1 + ground_iah;
            } else {
                v0 = 1.0f - (float)(tr + 1) * iah;
                v1 = 1.0f - (float)tr * iah;
            }
        }

        float tx   = (float)vt.col;
        float ty_f = (float)vt.row;
        uint8_t* p = ibuf + i * TINST_STRIDE;
        memcpy(p + TINST_OFF_POS + 0,  &tx,       4);
        memcpy(p + TINST_OFF_POS + 4,  &ty_f,     4);
        memcpy(p + TINST_OFF_UV  + 0,  &u0,       4);
        memcpy(p + TINST_OFF_UV  + 4,  &v0,       4);
        memcpy(p + TINST_OFF_UV  + 8,  &u1,       4);
        memcpy(p + TINST_OFF_UV  + 12, &v1,       4);
        memcpy(p + TINST_OFF_H,        &tile_h_f, 4);
    }

    if (n == 0) return;

    // GL state for isometric tile rendering.
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glUseProgram(prog_);

    Camera3D rl = cam.ToRaylib();
    Matrix V = MatrixLookAt(rl.position, rl.target, rl.up);
    Matrix P;
    if (ortho_size > 0.0f) {
        const float oh = ortho_size, ow = oh * aspect;
        P = MatrixOrtho(-ow, +ow, -oh, +oh, 0.1, 300.0);
    } else {
        P = MatrixPerspective((double)rl.fovy * 0.01745329251844, (double)aspect, 0.1, 300.0);
    }
    Matrix vp = MatrixMultiply(V, P);
    glUniformMatrix4fv(loc_vp_, 1, GL_FALSE, MatrixToFloat(vp));

    float tsz[2] = { tile_world_size, tile_world_size };
    glUniform2fv(loc_tile_size_, 1, tsz);
    glUniform1f (loc_y_, 0.0f);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);

    // PASS 4: draw contiguous atlas runs in painter's depth order.
    // Tiles are sorted by depth (col+row); we emit one draw call per run of
    // same atlas_idx, switching the bound texture between runs.
    // This maintains correct painter's order across multiple atlases.
    {
        int i = 0;
        while (i < n) {
            uint8_t aidx       = vbuf[i].atlas_idx;
            int     batch_start = i;
            while (i < n && vbuf[i].atlas_idx == aidx) ++i;
            int count = i - batch_start;

            // Upload only the relevant batch slice to the VBO start.
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)(count * TINST_STRIDE),
                            ibuf + batch_start * TINST_STRIDE);

            if (aidx < (uint8_t)atlas_count_ && atlases_[aidx].id)
                MdBindTexture(atlases_[aidx], 0);

            glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, count);
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glUseProgram(0);
}

} // namespace md::flare
#endif // MD_OPENGL43_ENABLED
