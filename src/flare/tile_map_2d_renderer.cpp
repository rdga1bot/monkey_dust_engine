#include <monkey_dust/flare/tile_map_2d_renderer.h>
#include <monkey_dust/render/md_shader.h>

#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "glad.h"

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#endif

// ── Shared helpers ────────────────────────────────────────────────────────────

static constexpr int TILE_W_HALF = 96;
static constexpr int TILE_H_HALF = 48;

struct Tile2D {
    int   col, row, tid, layer_prio;
    float depth;
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

// ── Init ──────────────────────────────────────────────────────────────────────

void TileMap2DRenderer::Init() {
    if (init_) return;

#ifdef MD_SDL_GPU
    if (md::GpuDevice::Get().IsReady()) {
        GpuPipeline::Desc pd;
        pd.vert_path           = "shaders/tile_map_2d.vert";
        pd.frag_path           = "shaders/tile_map_2d.frag";
        pd.raster.topology     = GpuTopology::TRIANGLES;
        pd.raster.blend_enable = true;
        pd.raster.src_factor   = GpuBlendFactor::SRC_ALPHA;
        pd.raster.dst_factor   = GpuBlendFactor::ONE_MINUS_SRC_ALPHA;
        pd.raster.depth_test   = false;
        pd.raster.depth_write  = false;
        pd.raster.cull_back    = false;
        pd.vert_uniform_bufs   = 1;  // set=1,binding=0: {vec2 viewport, vec2 pad}
        pd.frag_samplers       = 4;  // set=2,binding=0..3: atlases
        pd.has_depth_target    = false;
        pd.layout.stride       = (uint32_t)STRIDE_SDL;
        pd.layout.count        = 5;
        pd.layout.attribs[0]   = {0,  0, GpuAttribFmt::F2};  // a_corner
        pd.layout.attribs[1]   = {1,  8, GpuAttribFmt::F2};  // a_screen_tl
        pd.layout.attribs[2]   = {2, 16, GpuAttribFmt::F2};  // a_screen_size
        pd.layout.attribs[3]   = {3, 24, GpuAttribFmt::F4};  // a_uv_rect
        pd.layout.attribs[4]   = {4, 40, GpuAttribFmt::F1};  // a_atlas_idx

        if (!sdl_pipeline_.Create(pd))
            fprintf(stderr, "[TileMap2D] SDL_GPU: pipeline create failed\n");

        sdl_vbuf_.Init((uint32_t)MAX_TILES * 6u, (uint32_t)STRIDE_SDL);

        // 1×1 opaque white dummy: transparent would be discarded (c.a<0.1) by the
        // tile shader, and NPC dots (atlas_idx=atlas_count_) rely on this slot.
        GpuSamplerDesc ds;
        ds.min_filter = GpuSamplerDesc::Filter::NEAREST;
        ds.mag_filter = GpuSamplerDesc::Filter::NEAREST;
        ds.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        ds.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        ds.gen_mipmap = false;
        ds.flip_v     = false;
        uint8_t pix[4] = {255, 255, 255, 255};
        GpuTexture dgt;
        if (dgt.InitFromMemory(pix, 1, 1, ds)) {
            sdl_dummy_tex_     = dgt.TakeSDLTexture();
            sdl_dummy_sampler_ = dgt.TakeSDLSampler();
        }
        sdl_init_ = true;
        init_     = true;
        return;
    }
#endif

    {
        static const float QUAD[8] = { 0,0, 1,0, 1,1, 0,1 };
        glGenBuffers(1, &quad_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);

        glGenBuffers(1, &inst_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAX_TILES * STRIDE), nullptr, GL_STREAM_DRAW);

        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
        glVertexAttribDivisor(0, 0);

        glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)0);
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)8);
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, STRIDE, (void*)16);
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, STRIDE, (void*)(intptr_t)32);
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
            fprintf(stderr, "[TileMap2D] GL: failed to load shaders\n");
        }
    }
    init_ = true;
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void TileMap2DRenderer::Shutdown() {
    if (!init_) return;
    for (int i = 0; i < atlas_count_; ++i) MdUnloadTexture(atlases_[i]);
    atlas_count_ = 0;

#ifdef MD_SDL_GPU
    if (sdl_init_) {
        sdl_pipeline_.Destroy();
        sdl_vbuf_.Shutdown();
        SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
        if (dev) {
            if (sdl_dummy_sampler_) SDL_ReleaseGPUSampler(dev, (SDL_GPUSampler*)sdl_dummy_sampler_);
            if (sdl_dummy_tex_)     SDL_ReleaseGPUTexture(dev, (SDL_GPUTexture*)sdl_dummy_tex_);
        }
        sdl_dummy_tex_ = sdl_dummy_sampler_ = nullptr;
        sdl_init_ = false;
    }
#endif

    if (vao_)      { glDeleteVertexArrays(1, &vao_);      vao_      = 0; }
    if (quad_vbo_) { glDeleteBuffers(1, &quad_vbo_);      quad_vbo_ = 0; }
    if (inst_vbo_) { glDeleteBuffers(1, &inst_vbo_);      inst_vbo_ = 0; }
    if (prog_)     { glDeleteProgram(prog_);               prog_     = 0; }

    init_ = false;
}

// ── SetAtlases ────────────────────────────────────────────────────────────────

void TileMap2DRenderer::SetAtlases(const FlareMap& map) {
    for (int i = 0; i < atlas_count_; ++i) MdUnloadTexture(atlases_[i]);
    atlas_count_ = 0;
    for (int i = 0; i < map.tileset_atlas_count && i < MAX_ATLAS; ++i) {
        if (!map.tileset_atlases[i][0]) { ++atlas_count_; continue; }
        atlases_[atlas_count_] = MdLoadTexturePixelArt(map.tileset_atlases[i]);
        bool valid = (atlases_[atlas_count_].id != 0);
#ifdef MD_SDL_GPU
        valid = valid || (atlases_[atlas_count_].sdl_tex != nullptr);
#endif
        if (!valid)
            fprintf(stderr, "[TileMap2D] atlas[%d] failed: %s\n", i, map.tileset_atlases[i]);
        ++atlas_count_;
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

void TileMap2DRenderer::SetNpcDots(const float* tile_x, const float* tile_z,
                                    int count, float dot_px)
{
    npc_dot_count_ = count < MAX_NPC_DOTS ? count : MAX_NPC_DOTS;
    for (int i = 0; i < npc_dot_count_; ++i) {
        npc_dot_x_[i] = tile_x[i];
        npc_dot_z_[i] = tile_z[i];
    }
    npc_dot_px_ = dot_px;
}

void TileMap2DRenderer::Render(const FlareMap& map, float now_s,
                                float origin_x, float origin_y, float scale,
                                int vp_w, int vp_h, uint8_t layer_mask)
{
    if (!init_ || atlas_count_ == 0) return;
    if (map.layer_count == 0 || map.width <= 0 || map.height <= 0) return;

#ifdef MD_SDL_GPU
    if (sdl_init_) {
        SDL_GPUCommandBuffer* cmd = md::GpuDevice::Get().AcquireCommandBuffer();
        if (!cmd) return;
        uint32_t sw = 0, sh = 0;
        SDL_GPUTexture* swap = md::GpuDevice::Get().AcquireSwapchainTexture(cmd, &sw, &sh);
        if (!swap) { md::GpuDevice::Get().Submit(cmd); return; }
        RenderSDLGPU(map, now_s, origin_x, origin_y, scale, vp_w, vp_h, layer_mask, cmd, swap);
        md::GpuDevice::Get().Submit(cmd);
        return;
    }
#endif

    if (!prog_) return;

    // ── Collect + sort tiles ──────────────────────────────────────────────────
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
                if (tid == 0 || !map.meta.Find(tid)) continue;
                tiles[n++] = { col, row, tid, prio, (float)((col + row) * 3 + prio) };
            }
        }
    }
    qsort(tiles, (size_t)n, sizeof(Tile2D), Tile2DCmp);

    // ── Build instance buffer (stride=36) ─────────────────────────────────────
    static uint8_t ibuf[MAX_TILES * STRIDE];
    int ni = 0;

    for (int i = 0; i < n; ++i) {
        const Tile2D& t    = tiles[i];
        const TileMeta* tm = map.meta.Find(t.tid);
        if (!tm) continue;

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

        float ax   = (float)((t.col - t.row) * TILE_W_HALF) * scale + origin_x;
        float ay   = (float)((t.col + t.row) * TILE_H_HALF) * scale + origin_y;
        float x_tl = ax - (float)tm->offset_x * scale;
        float y_tl = ay - (float)tm->offset_y * scale;
        float sw2  = (float)tm->w * scale;
        float sh2  = (float)tm->h * scale;
        float u0 = (float)sx / (float)atl.w;
        float u1 = (float)(sx + tm->w) / (float)atl.w;
        float v0 = 1.0f - (float)sy / (float)atl.h;
        float v1 = 1.0f - (float)(sy + tm->h) / (float)atl.h;
        float ai = (float)aidx;

        uint8_t* p = ibuf + ni * STRIDE;
        memcpy(p +  0, &x_tl, 4);  memcpy(p +  4, &y_tl, 4);
        memcpy(p +  8, &sw2,  4);  memcpy(p + 12, &sh2,  4);
        memcpy(p + 16, &u0,   4);  memcpy(p + 20, &v0,   4);
        memcpy(p + 24, &u1,   4);  memcpy(p + 28, &v1,   4);
        memcpy(p + 32, &ai,   4);
        ++ni;
    }
    n = ni;

    // ── Upload + draw ─────────────────────────────────────────────────────────
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * STRIDE), ibuf);

    GLint prev_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);

    float vp[2] = { (float)vp_w, (float)vp_h };
    glUniform2fv(loc_viewport_, 1, vp);

    for (int i = 0; i < atlas_count_ && i < MAX_ATLAS; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, atlases_[i].id);
        if (loc_atlas_[i] >= 0) glUniform1i(loc_atlas_[i], i);
    }

    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, n);
    glBindVertexArray(0);

    // Restore GL state for any subsequent draw calls.
    glUseProgram((GLuint)prev_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
}

// ── Atlas accessors (non-inline — struct layout differs between engine and editor) ──

MdTexture TileMap2DRenderer::GetAtlas(int idx) const {
    if (idx < 0 || idx >= atlas_count_) return {};
    return atlases_[idx];
}
int TileMap2DRenderer::GetAtlasCount() const { return atlas_count_; }

// ── RenderSDLGPU ──────────────────────────────────────────────────────────────

#ifdef MD_SDL_GPU
void TileMap2DRenderer::RenderSDLGPU(const FlareMap& map, float now_s,
                                      float origin_x, float origin_y, float scale,
                                      int vp_w, int vp_h, uint8_t layer_mask,
                                      SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swap_tex)
{
    // Collect + sort tiles (SDL_GPU path owns its own scratch to avoid aliasing
    // with the static tiles[] in the GL path when both are compiled).
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
                if (tid == 0 || !map.meta.Find(tid)) continue;
                tiles[n++] = { col, row, tid, prio, (float)((col + row) * 3 + prio) };
            }
        }
    }
    qsort(tiles, (size_t)n, sizeof(Tile2D), Tile2DCmp);

    // Corners for 2 CCW triangles (TRIANGLE_LIST).
    static const float CORNERS[6][2] = {
        {0.f,0.f}, {1.f,0.f}, {1.f,1.f},
        {0.f,0.f}, {1.f,1.f}, {0.f,1.f}
    };

    // Build flat vertex buffer: 6 verts × STRIDE_SDL bytes per tile.
    static uint8_t scratch[MAX_TILES * 6 * STRIDE_SDL];
    int ni = 0;

    for (int i = 0; i < n && ni < MAX_TILES; ++i) {
        const Tile2D& t    = tiles[i];
        const TileMeta* tm = map.meta.Find(t.tid);
        if (!tm) continue;

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
        if (!atl.sdl_tex || atl.w <= 0 || atl.h <= 0) continue;
        if (sx + tm->w > atl.w || sy + tm->h > atl.h) continue;

        float ax   = (float)((t.col - t.row) * TILE_W_HALF) * scale + origin_x;
        float ay   = (float)((t.col + t.row) * TILE_H_HALF) * scale + origin_y;
        float x_tl = ax - (float)tm->offset_x * scale;
        float y_tl = ay - (float)tm->offset_y * scale;
        float sw   = (float)tm->w * scale;
        float sh   = (float)tm->h * scale;

        // UV: stbi flip active → v_gl = 1 − y_file / H.
        float u0 = (float)sx / (float)atl.w;
        float u1 = (float)(sx + tm->w) / (float)atl.w;
        float v0 = 1.0f - (float)sy / (float)atl.h;
        float v1 = 1.0f - (float)(sy + tm->h) / (float)atl.h;
        float ai = (float)aidx;

        for (int vi = 0; vi < 6; ++vi) {
            float* v = (float*)(scratch + ((size_t)ni * 6 + (size_t)vi) * (size_t)STRIDE_SDL);
            v[0]  = CORNERS[vi][0];  // a_corner.x
            v[1]  = CORNERS[vi][1];  // a_corner.y
            v[2]  = x_tl;           // a_screen_tl.x
            v[3]  = y_tl;           // a_screen_tl.y
            v[4]  = sw;             // a_screen_size.x
            v[5]  = sh;             // a_screen_size.y
            v[6]  = u0;             // a_uv_rect.x
            v[7]  = v0;             // a_uv_rect.y
            v[8]  = u1;             // a_uv_rect.z
            v[9]  = v1;             // a_uv_rect.w
            v[10] = ai;             // a_atlas_idx
        }
        ++ni;
    }

    // ── NPC dots: white quads using dummy atlas slot (atlas_count_ index) ─────
    // atlas_idx = atlas_count_ → bound to sdl_dummy_tex_ (1×1 white, alpha=1).
    if (npc_dot_count_ > 0 && ni < MAX_TILES) {
        float ai  = (float)atlas_count_;  // dummy slot — always white
        float dpx = npc_dot_px_ > 1.f ? npc_dot_px_ : 1.f;
        for (int di = 0; di < npc_dot_count_ && ni < MAX_TILES; ++di) {
            float col = npc_dot_x_[di], row = npc_dot_z_[di];
            float ax  = (float)((col - row) * (float)TILE_W_HALF) * scale + origin_x;
            float ay  = (float)((col + row) * (float)TILE_H_HALF) * scale + origin_y;
            // center the dot on the tile diamond
            float x_tl = ax + (float)TILE_W_HALF * scale * 0.5f - dpx * 0.5f;
            float y_tl = ay + (float)TILE_H_HALF * scale * 0.5f - dpx * 0.5f;
            for (int vi = 0; vi < 6; ++vi) {
                float* v = (float*)(scratch + ((size_t)ni * 6 + (size_t)vi) * (size_t)STRIDE_SDL);
                v[0]  = CORNERS[vi][0];
                v[1]  = CORNERS[vi][1];
                v[2]  = x_tl;
                v[3]  = y_tl;
                v[4]  = dpx;
                v[5]  = dpx;
                v[6]  = 0.f;   // uv min
                v[7]  = 0.f;
                v[8]  = 1.f;   // uv max
                v[9]  = 1.f;
                v[10] = ai;
            }
            ++ni;
        }
    }

    // ── Correct order: copy pass first, then render pass ─────────────────────
    // SDL_GPU requires the copy pass to precede the render pass on the same cmd.
    if (ni > 0) {
        void* ptr = sdl_vbuf_.MapWrite();
        if (ptr) {
            memcpy(ptr, scratch, (size_t)ni * 6u * (size_t)STRIDE_SDL);
            sdl_vbuf_.Unmap();
        }
        sdl_vbuf_.Upload(cmd);  // opens + closes a copy pass on cmd
    }

    // Render pass: clear swapchain + optionally draw tiles.
    GpuCommandBuffer cb;
    GpuCommandBuffer::ColorPassDesc cpd;
    cpd.cmd            = cmd;
    cpd.color_tex      = swap_tex;
    cpd.clear_color[0] = 0.08f;
    cpd.clear_color[1] = 0.08f;
    cpd.clear_color[2] = 0.12f;
    cpd.clear_color[3] = 1.0f;
    cpd.load_color     = false;
    cb.BeginColorPass(cpd);

    if (ni > 0) {
        cb.BindPipeline(&sdl_pipeline_);
        cb.BindVertexBuffer(&sdl_vbuf_);

        // Bind 4 atlas samplers; pad unused slots with the 1×1 dummy texture.
        SDL_GPUTextureSamplerBinding bindings[4];
        for (int i = 0; i < 4; ++i) {
            bool has = (i < atlas_count_) && atlases_[i].sdl_tex;
            bindings[i].texture = has ? (SDL_GPUTexture*)atlases_[i].sdl_tex
                                      : (SDL_GPUTexture*)sdl_dummy_tex_;
            bindings[i].sampler = has ? (SDL_GPUSampler*)atlases_[i].sdl_sampler
                                      : (SDL_GPUSampler*)sdl_dummy_sampler_;
        }
        cb.BindFragmentSamplers(0, bindings, 4);

        // Push viewport UBO (set=1, binding=0): {vec2 viewport, vec2 pad} = 16 bytes std140.
        float vp_ubo[4] = { (float)vp_w, (float)vp_h, 0.f, 0.f };
        cb.PushVertexUniforms(0, vp_ubo, 16);

        cb.Draw((uint32_t)ni * 6u);
    }
    cb.EndPass();
}
#endif // MD_SDL_GPU

} // namespace md::flare

