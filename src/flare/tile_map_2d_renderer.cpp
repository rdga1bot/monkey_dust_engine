#include <monkey_dust/flare/tile_map_2d_renderer.h>
#include <monkey_dust/render/md_shader.h>

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#include <cstring>
#include <cstdlib>  // qsort
#include <cstdio>

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr int TILE_W_HALF = 96;   // TILE_W/2 = 192/2
static constexpr int TILE_H_HALF = 48;   // TILE_H/2 =  96/2

// Per-instance offsets (stride = 36)
static constexpr int OFF_TL    =  0;  // vec2 screen_tl
static constexpr int OFF_SIZE  =  8;  // vec2 screen_size
static constexpr int OFF_UV    = 16;  // vec4 uv_rect
static constexpr int OFF_AIDX  = 32;  // float atlas_idx
static constexpr int STRIDE    = 36;

static const float QUAD[8] = { 0,0, 1,0, 1,1, 0,1 };

// ── Depth sort scratch ────────────────────────────────────────────────────────

struct Tile2D {
    int   col, row;
    int   tid;
    int   layer_prio;    // 0=bg, 1=fringe, 2=object
    float depth;         // (col+row)*3 + layer_prio
};

static int Tile2DCmp(const void* a, const void* b) {
    float da = ((const Tile2D*)a)->depth;
    float db = ((const Tile2D*)b)->depth;
    return (da > db) - (da < db);
}

static int LayerPrio2D(md::flare::LayerType t) {
    using LT = md::flare::LayerType;
    switch (t) {
        case LT::BACKGROUND: return 0;
        case LT::FRINGE:     return 1;
        case LT::OBJECT:     return 2;
        default:             return 1;
    }
}

namespace md::flare {

TileMap2DRenderer& TileMap2DRenderer::Get() {
    static TileMap2DRenderer inst;
    return inst;
}

void TileMap2DRenderer::Init() {
    if (init_) return;

    glGenBuffers(1, &quad_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);

    glGenBuffers(1, &inst_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAX_TILES * STRIDE), nullptr, GL_STREAM_DRAW);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    // attrib 0: quad corner (per-vertex)
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glVertexAttribDivisor(0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);

    // attrib 1: screen_tl (vec2, offset 0)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)OFF_TL);
    glVertexAttribDivisor(1, 1);

    // attrib 2: screen_size (vec2, offset 8)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)OFF_SIZE);
    glVertexAttribDivisor(2, 1);

    // attrib 3: uv_rect (vec4, offset 16)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, STRIDE, (void*)OFF_UV);
    glVertexAttribDivisor(3, 1);

    // attrib 4: atlas_idx (float, offset 32)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(intptr_t)OFF_AIDX);
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);

    MdShader sh = MdLoadShader("shaders/tile_map_2d.vert", "shaders/tile_map_2d.frag");
    prog_ = sh.id;
    if (prog_) {
        loc_viewport_ = MdGetLoc(sh, "u_viewport");
        loc_atlas_[0] = MdGetLoc(sh, "u_atlas0");
        loc_atlas_[1] = MdGetLoc(sh, "u_atlas1");
        loc_atlas_[2] = MdGetLoc(sh, "u_atlas2");
        loc_atlas_[3] = MdGetLoc(sh, "u_atlas3");
    } else {
        fprintf(stderr, "[TileMap2D] failed to load shaders\n");
    }

    init_ = true;
}

void TileMap2DRenderer::Shutdown() {
    if (!init_) return;
    for (int i = 0; i < atlas_count_; ++i) MdUnloadTexture(atlases_[i]);
    atlas_count_ = 0;
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &quad_vbo_);
    glDeleteBuffers(1, &inst_vbo_);
    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
    init_ = false;
}

void TileMap2DRenderer::SetAtlases(const FlareMap& map) {
    for (int i = 0; i < atlas_count_; ++i) MdUnloadTexture(atlases_[i]);
    atlas_count_ = 0;
    for (int i = 0; i < map.tileset_atlas_count && i < MAX_ATLAS; ++i) {
        if (!map.tileset_atlases[i][0]) { ++atlas_count_; continue; }
        atlases_[atlas_count_] = MdLoadTexturePixelArt(map.tileset_atlases[i]);
        if (!atlases_[atlas_count_].id)
            fprintf(stderr, "[TileMap2D] atlas[%d] failed: %s\n", i, map.tileset_atlases[i]);
        ++atlas_count_;
    }
}

void TileMap2DRenderer::Render(const FlareMap& map, float now_s,
                                float origin_x, float origin_y, float scale,
                                int vp_w, int vp_h, uint8_t layer_mask)
{
    if (!init_ || !prog_ || atlas_count_ == 0) return;
    if (map.layer_count == 0 || map.width <= 0 || map.height <= 0) return;

    // ── Collect visible tiles ─────────────────────────────────────────────────

    static Tile2D tiles[MAX_TILES];
    int n = 0;

    for (int li = 0; li < map.layer_count && n < MAX_TILES; ++li) {
        if (!(layer_mask & (1u << li))) continue;
        const TileMapLayer& layer = map.layers[li];
        using LT = md::flare::LayerType;
        if (layer.type == LT::COLLISION) continue;
        if (layer.type != LT::BACKGROUND && layer.type != LT::FRINGE && layer.type != LT::OBJECT) continue;
        int prio = LayerPrio2D(layer.type);
        for (int row = 0; row < map.height && n < MAX_TILES; ++row) {
            for (int col = 0; col < map.width && n < MAX_TILES; ++col) {
                int tid = layer.tiles[row * MAX_MAP_WIDTH + col];
                if (tid == 0) continue;
                if (!map.meta.Find(tid)) continue;
                tiles[n].col        = col;
                tiles[n].row        = row;
                tiles[n].tid        = tid;
                tiles[n].layer_prio = prio;
                tiles[n].depth      = (float)((col + row) * 3 + prio);
                ++n;
            }
        }
    }

    qsort(tiles, (size_t)n, sizeof(Tile2D), Tile2DCmp);

    // ── Build instance buffer ─────────────────────────────────────────────────

    static uint8_t ibuf[MAX_TILES * STRIDE];
    int ni = 0;  // write index — separate from read index to avoid gaps on skip

    for (int i = 0; i < n; ++i) {
        const Tile2D& t    = tiles[i];
        const TileMeta* tm = map.meta.Find(t.tid);
        if (!tm) continue;

        // Animation: resolve current frame src_x/src_y.
        int16_t sx = tm->src_x, sy = tm->src_y;
        if (now_s > 0.0f) {
            const TileAnim* anim = map.meta.FindAnim(t.tid);
            if (anim && anim->total_ms > 0) {
                uint32_t ms    = (uint32_t)(now_s * 1000.0f) % anim->total_ms;
                uint32_t accum = 0;
                for (int fi = 0; fi < anim->frame_count; ++fi) {
                    accum += anim->frames[fi].duration_ms;
                    if (ms < accum) { sx = anim->frames[fi].src_x; sy = anim->frames[fi].src_y; break; }
                }
            }
        }

        uint8_t aidx = tm->atlas_idx < (uint8_t)atlas_count_ ? tm->atlas_idx : 0;
        const MdTexture& atl = atlases_[aidx];
        if (!atl.id || atl.w <= 0 || atl.h <= 0) continue;
        if (sx + tm->w > atl.w || sy + tm->h > atl.h) continue;

        // Exact Flare 2D screen formula (scaled).
        float ax  = (float)((t.col - t.row) * TILE_W_HALF) * scale + origin_x;
        float ay  = (float)((t.col + t.row) * TILE_H_HALF) * scale + origin_y;
        float x_tl = ax - (float)tm->offset_x * scale;
        float y_tl = ay - (float)tm->offset_y * scale;
        float sw   = (float)tm->w * scale;
        float sh   = (float)tm->h * scale;

        // UV (stbi flip active: v_gl = 1 - y_file/H).
        float u0 = (float)sx / (float)atl.w;
        float u1 = (float)(sx + tm->w) / (float)atl.w;
        float v0 = 1.0f - (float)sy / (float)atl.h;
        float v1 = 1.0f - (float)(sy + tm->h) / (float)atl.h;
        float ai = (float)aidx;

        uint8_t* p = ibuf + ni * STRIDE;  // write to ni, not i
        memcpy(p + OFF_TL + 0,   &x_tl, 4);
        memcpy(p + OFF_TL + 4,   &y_tl, 4);
        memcpy(p + OFF_SIZE + 0,  &sw,   4);
        memcpy(p + OFF_SIZE + 4,  &sh,   4);
        memcpy(p + OFF_UV + 0,   &u0,   4);
        memcpy(p + OFF_UV + 4,   &v0,   4);
        memcpy(p + OFF_UV + 8,   &u1,   4);
        memcpy(p + OFF_UV + 12,  &v1,   4);
        memcpy(p + OFF_AIDX,     &ai,   4);
        ++ni;
    }
    n = ni;  // actual draw count

    // ── Upload & draw ─────────────────────────────────────────────────────────

    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * STRIDE), ibuf);

    // Save Raylib's GL state before taking over the pipeline.
    GLint prev_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(prog_);

    float vp[2] = { (float)vp_w, (float)vp_h };
    glUniform2fv(loc_viewport_, 1, vp);

    // Bind all atlases to texture units 0-3.
    for (int i = 0; i < atlas_count_ && i < MAX_ATLAS; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, atlases_[i].id);
        if (loc_atlas_[i] >= 0) glUniform1i(loc_atlas_[i], i);
    }

    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, n);
    glBindVertexArray(0);

    // Restore GL state for Raylib's subsequent DrawText / DrawFPS calls.
    // Critical: keep GL_BLEND enabled with alpha blend func — Raylib's font
    // glyphs are RGBA with transparency; disabling blend renders them as
    // solid white rectangles ("boxes").
    glUseProgram((GLuint)prev_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);  // unbind atlas so Raylib rebinds font tex
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
}

} // namespace md::flare

#else
// Stub for non-OpenGL builds.
namespace md::flare {
TileMap2DRenderer& TileMap2DRenderer::Get() { static TileMap2DRenderer i; return i; }
void TileMap2DRenderer::Init() {}
void TileMap2DRenderer::Shutdown() {}
void TileMap2DRenderer::SetAtlases(const FlareMap&) {}
void TileMap2DRenderer::Render(const FlareMap&, float, float, float, float, int, int) {}
} // namespace md::flare
#endif
