#include <monkey_dust/flare/tile_map_renderer.h>
#include <monkey_dust/render/md_shader.h>

#ifdef MD_OPENGL43_ENABLED
#include "external/glad.h"
#include "raylib.h"   // Matrix, MatrixMultiply
#include <cstring>
#include <cstdio>

// Per-instance GPU layout (stride = 24 bytes):
//   vec2  tile_pos  (grid x, y)   offset  0
//   vec4  uv_rect   (u0,v0,u1,v1) offset  8
static constexpr int TINST_STRIDE  = 24;
static constexpr int TINST_OFF_POS =  0;
static constexpr int TINST_OFF_UV  =  8;

// Unit quad corners [0,1]×[0,1] (4 verts, GL_TRIANGLE_FAN).
static const float TILE_QUAD[8] = {
    0.f, 0.f,  1.f, 0.f,  1.f, 1.f,  0.f, 1.f
};

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

    glBindVertexArray(0);

    MdShader sh = MdLoadShader("shaders/tile_map.vert", "shaders/tile_map.frag");
    prog_ = sh.id;
    if (prog_) {
        loc_view_      = MdGetLoc(sh, "u_view");
        loc_proj_      = MdGetLoc(sh, "u_proj");
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
    atlas_ = MdLoadTexture(png_path);
    if (!atlas_.id)
        fprintf(stderr, "[TileMap] failed to load atlas: %s\n", png_path);
}

void TileMapRenderer::Render(const FlareMap& map, const MdCamera& cam,
                              float aspect, float tile_world_size)
{
    if (!init_ || !prog_) return;
    if (map.layer_count == 0 || map.width <= 0 || map.height <= 0) return;

    // Pick background layer (first BACKGROUND, else layer 0).
    const TileMapLayer* bg = nullptr;
    for (int i = 0; i < map.layer_count; ++i)
        if (map.layers[i].type == LayerType::BACKGROUND) { bg = &map.layers[i]; break; }
    if (!bg) bg = &map.layers[0];

    // Identify the main visual tileset (lowest firstgid ≥ 2, i.e. not collision).
    int ref_ts = 0;
    for (int i = 0; i < map.tileset_count; ++i) {
        if (map.tilesets[i].firstgid >= 2) { ref_ts = i; break; }
    }
    const TileSet& ts = map.tilesets[ref_ts];
    int cols = ts.columns > 0 ? ts.columns : 16;

    // Atlas UV step (normalized).
    float inv_aw = (atlas_.w > 0 && ts.tile_w > 0)
                   ? (float)ts.tile_w / (float)atlas_.w : 1.0f / (float)cols;
    float inv_ah = (atlas_.h > 0 && ts.tile_h > 0)
                   ? (float)ts.tile_h / (float)atlas_.h : inv_aw;

    // Pack instance buffer.
    static uint8_t ibuf[MAX_VISIBLE_TILES * TINST_STRIDE];
    int n = 0;

    for (int row = 0; row < map.height && row < MAX_MAP_HEIGHT && n < MAX_VISIBLE_TILES; ++row) {
        for (int col = 0; col < map.width && col < MAX_MAP_WIDTH && n < MAX_VISIBLE_TILES; ++col) {
            uint16_t tid = bg->tiles[row * MAX_MAP_WIDTH + col];
            if (tid == 0) continue;

            int ts_idx = 0, local_idx = 0;
            if (!ResolveTile(map, tid, &ts_idx, &local_idx)) continue;

            // Skip collision tileset (firstgid=1) for visual rendering.
            if (map.tilesets[ts_idx].firstgid == 1) continue;

            const TileSet& cur = map.tilesets[ts_idx];
            int c = cur.columns > 0 ? cur.columns : cols;
            float iaw = (atlas_.w > 0 && cur.tile_w > 0) ? (float)cur.tile_w / atlas_.w : inv_aw;
            float iah = (atlas_.h > 0 && cur.tile_h > 0) ? (float)cur.tile_h / atlas_.h : inv_ah;

            int tc = local_idx % c;
            int tr = local_idx / c;
            float u0 = tc * iaw;
            float v0 = tr * iah;
            float u1 = u0 + iaw;
            float v1 = v0 + iah;

            uint8_t* p = ibuf + n * TINST_STRIDE;
            float tx = (float)col;
            float ty_f = (float)row;
            memcpy(p + TINST_OFF_POS + 0, &tx,  4);
            memcpy(p + TINST_OFF_POS + 4, &ty_f, 4);
            memcpy(p + TINST_OFF_UV +  0, &u0, 4);
            memcpy(p + TINST_OFF_UV +  4, &v0, 4);
            memcpy(p + TINST_OFF_UV +  8, &u1, 4);
            memcpy(p + TINST_OFF_UV + 12, &v1, 4);
            ++n;
        }
    }

    if (n == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * TINST_STRIDE), ibuf);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

#ifdef USE_GLM
    Matrix view = GlmToRaylibMat(cam.ViewMatrix());
    Matrix proj = GlmToRaylibMat(cam.ProjMatrix(aspect));
#else
    Matrix view = cam.ViewMatrix();
    Matrix proj = cam.ProjMatrix(aspect);
#endif

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glUseProgram(prog_);

    glUniformMatrix4fv(loc_view_, 1, GL_FALSE, &view.m0);
    glUniformMatrix4fv(loc_proj_, 1, GL_FALSE, &proj.m0);

    float tsz[2] = { tile_world_size, tile_world_size };
    glUniform2fv(loc_tile_size_, 1, tsz);
    glUniform1f (loc_y_, 0.0f);

    if (atlas_.id) MdBindTexture(atlas_, 0);

    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, n);
    glBindVertexArray(0);

    glUseProgram(0);
}

} // namespace md::flare
#endif // MD_OPENGL43_ENABLED
