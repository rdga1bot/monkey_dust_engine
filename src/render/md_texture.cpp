#include <monkey_dust/render/md_texture.h>

#include <cstdio>
#include <monkey_dust/render/gpu_hal.h>
#include "glad.h"
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <SDL3/SDL_gpu.h>
#endif
#ifdef MD_USE_LIBGODOT
// Фаза C: RenderingServer-backed path, bypasses GpuTexture entirely --
// decodes via stb_image directly (same stbi_load(path,&w,&h,&ch,4)
// pattern as gpu_hal_buffers_texture_core.cpp/rd_texture.cpp), builds a
// Ref<Image>, uploads via texture_2d_create(). stb_image.h is header-only
// here (no STBI_IMPLEMENTATION -- the final binary's own
// stb_image_impl.cpp supplies it, same convention as every other TU).
#include "stb_image.h"
#include "servers/rendering/rendering_server.h"
#include "core/io/image.h"
#endif

// MdTexture functions delegate to GpuTexture so both GL and SDL_GPU
// handles are captured.  Transfer methods (Release / TakeSDL*) zero
// out the source so the GpuTexture destructor does nothing.
//
// CONVENTION (DO NOT CHANGE without updating tile_map_renderer.cpp UV formulas):
//   flip_v=true: GL/SDL_GPU v=0 = bottom of image file, v=1 = top.
//   Tile UV formula: v_gl = 1.0f - y_file / atlas_h
//   Removing the flip makes ALL tile UVs wrong.

// ── Texture cache ─────────────────────────────────────────────────────────────

static constexpr int MAX_TEX_CACHE = 128;

struct TexCacheEntry {
    uint32_t  hash = 0;
    MdTexture tex  = {};
    bool      used = false;
};

static TexCacheEntry s_cache_pa [MAX_TEX_CACHE];  // MdLoadTexturePixelArt
static TexCacheEntry s_cache_lin[MAX_TEX_CACHE];  // MdLoadTexture (linear)

static uint32_t TexHash(const char* path) {
    uint32_t h = 2166136261u;
    for (const char* p = path; *p; ++p)
        h = (h ^ (uint8_t)*p) * 16777619u;
    return h ? h : 1u;  // 0 = empty sentinel
}

static const MdTexture* CacheFind(const TexCacheEntry* c, uint32_t hash) {
    for (int i = 0; i < MAX_TEX_CACHE; ++i)
        if (c[i].used && c[i].hash == hash) return &c[i].tex;
    return nullptr;
}

static void CacheInsert(TexCacheEntry* c, uint32_t hash, const MdTexture& t) {
    for (int i = 0; i < MAX_TEX_CACHE; ++i) {
        if (!c[i].used) { c[i] = {hash, t, true}; return; }
    }
    fprintf(stderr, "[TexCache] full (MAX=%d) — not cached\n", MAX_TEX_CACHE);
}

// Returns true if sdl_tex pointer matches any cached entry (cache owns it).
static bool CacheOwns(const MdTexture& t) {
#ifdef MD_USE_LIBGODOT
    if (!t.libgodot_tex_rid) return false;
    for (int i = 0; i < MAX_TEX_CACHE; ++i) {
        if (s_cache_pa [i].used && s_cache_pa [i].tex.libgodot_tex_rid == t.libgodot_tex_rid) return true;
        if (s_cache_lin[i].used && s_cache_lin[i].tex.libgodot_tex_rid == t.libgodot_tex_rid) return true;
    }
    return false;
#else
    if (!t.sdl_tex && !t.id) return false;
    for (int i = 0; i < MAX_TEX_CACHE; ++i) {
        if (s_cache_pa[i].used &&
            (s_cache_pa[i].tex.sdl_tex == t.sdl_tex && t.sdl_tex) ||
            (s_cache_pa[i].tex.id      == t.id       && t.id))
            return true;
        if (s_cache_lin[i].used &&
            (s_cache_lin[i].tex.sdl_tex == t.sdl_tex && t.sdl_tex) ||
            (s_cache_lin[i].tex.id      == t.id       && t.id))
            return true;
    }
    return false;
#endif
}

static void ReleaseCacheArray(TexCacheEntry* c) {
    for (int i = 0; i < MAX_TEX_CACHE; ++i) {
        if (!c[i].used) continue;
        MdUnloadTexture(c[i].tex);   // safe: not in cache during Shutdown
        c[i] = {};
    }
}

// ─────────────────────────────────────────────────────────────────────────────

#ifdef MD_USE_LIBGODOT
// RGBA8 raw bytes -> RenderingServer texture RID. flip_v matches the
// GL/SDL_GPU CONVENTION at the top of this file (v=0 bottom-of-file).
static MdTexture LibGodotTextureFromRGBA8(const uint8_t* rgba8, int w, int h, bool flip_v) {
    MdTexture t;
    t.w = w; t.h = h;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs || w <= 0 || h <= 0) return t;

    Vector<uint8_t> data;
    data.resize(w * h * 4);
    uint8_t* dst = data.ptrw();
    if (flip_v) {
        for (int row = 0; row < h; ++row)
            memcpy(dst + (size_t)row * w * 4, rgba8 + (size_t)(h - 1 - row) * w * 4, (size_t)w * 4);
    } else {
        memcpy(dst, rgba8, (size_t)w * h * 4);
    }
    Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, data);
    RID tex = rs->texture_2d_create(img);
    t.libgodot_tex_rid = tex.get_id();
    return t;
}

static MdTexture LibGodotLoadFile(const char* path, bool flip_v) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0); // flip done manually above (matches GL/SDL_GPU byte order)
    uint8_t* data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) { fprintf(stderr, "[MdTexture] LibGodot load failed: %s\n", path); return {}; }
    MdTexture t = LibGodotTextureFromRGBA8(data, w, h, flip_v);
    stbi_image_free(data);
    return t;
}
#endif // MD_USE_LIBGODOT

static MdTexture FromGpuTexture(GpuTexture& gt) {
    MdTexture t;
    t.w  = gt.Width();
    t.h  = gt.Height();
    t.id = gt.Release();
#ifdef MD_SDL_GPU
    t.sdl_tex     = gt.TakeSDLTexture();
    t.sdl_sampler = gt.TakeSDLSampler();
#endif
    return t;
}

MdTexture MdLoadTexture(const char* path, float mip_lod_bias) {
    const uint32_t h = TexHash(path);
    if (const MdTexture* cached = CacheFind(s_cache_lin, h)) return *cached;

#ifdef MD_USE_LIBGODOT
    (void)mip_lod_bias; // RenderingServer generates its own mip chain -- no manual bias control here
    MdTexture t = LibGodotLoadFile(path, /*flip_v=*/true);
    if (!t.libgodot_tex_rid) return {};
#else
    GpuTexture gt;
    auto s = GpuSamplerDesc::Default();
    s.flip_v      = true;
    s.min_filter  = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    s.mag_filter  = GpuSamplerDesc::Filter::LINEAR;
    s.wrap_s      = GpuSamplerDesc::Wrap::REPEAT;
    s.wrap_t      = GpuSamplerDesc::Wrap::REPEAT;
    s.gen_mipmap  = true;
    s.mip_lod_bias = mip_lod_bias;
    if (!gt.InitFromFile(path, s)) return {};
    MdTexture t = FromGpuTexture(gt);
#endif
    CacheInsert(s_cache_lin, h, t);
    return t;
}

// FILTER RULES (DO NOT SIMPLIFY):
//   LINEAR_MIPMAP min + gen_mipmap: prevents GL_NEAREST vanishing at small sizes.
//   NEAREST mag: crisp pixel-art at zoom-in.
//   CLAMP_TO_EDGE: prevents atlas seam bleeding.
MdTexture MdLoadTexturePixelArt(const char* path) {
    const uint32_t h = TexHash(path);
    if (const MdTexture* cached = CacheFind(s_cache_pa, h)) return *cached;

#ifdef MD_USE_LIBGODOT
    // NOTE: RenderingServer texture_2d_create() takes sampler state from
    // the consuming material/shader, not the texture itself -- pixel-art
    // NEAREST/CLAMP filtering is NOT applied here (matches this Phase's
    // scope: MdTexture struct/signatures unchanged, caller code untouched;
    // real per-material sampler-state wiring is Фаза C.5 caller-migration
    // work, not this canonical-abstractions swap).
    MdTexture t = LibGodotLoadFile(path, /*flip_v=*/true);
    if (!t.libgodot_tex_rid) return {};
#else
    GpuTexture gt;
    if (!gt.InitFromFile(path, GpuSamplerDesc::PixelArt())) return {};
    MdTexture t = FromGpuTexture(gt);
#endif
    CacheInsert(s_cache_pa, h, t);
    return t;
}

MdTexture MdLoadTextureFromMemory(const uint8_t* data, int w, int h) {
#ifdef MD_USE_LIBGODOT
    return LibGodotTextureFromRGBA8(data, w, h, /*flip_v=*/false);
#else
    GpuTexture gt;
    if (!gt.InitFromMemory(data, w, h, GpuSamplerDesc::Lut())) return {};
    return FromGpuTexture(gt);
#endif
}

void MdUnloadTexture(MdTexture& t) {
    // If cache owns this texture, only zero the caller's copy.
    // GPU resources stay alive until MdTextureCache_Shutdown().
    if (CacheOwns(t)) { t = {}; return; }

#ifdef MD_USE_LIBGODOT
    if (t.libgodot_tex_rid) {
        RenderingServer* rs = RenderingServer::get_singleton();
        if (rs) rs->free_rid(RID::from_uint64(t.libgodot_tex_rid));
    }
#else
    if (t.id) { glDeleteTextures(1, &t.id); }
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        if (t.sdl_tex)     GpuReleaseTexture(dev, (SDL_GPUTexture*)t.sdl_tex);
        if (t.sdl_sampler) GpuReleaseSampler(dev, (SDL_GPUSampler*)t.sdl_sampler);
    }
#endif
#endif // MD_USE_LIBGODOT
    t = {};
}

void MdTextureCache_Shutdown() {
    // Count before release, then move to tmp arrays so CacheOwns()
    // returns false during MdUnloadTexture (avoids the cached-guard).
    int pa = 0, lin = 0;
    for (int i = 0; i < MAX_TEX_CACHE; ++i) {
        if (s_cache_pa [i].used) ++pa;
        if (s_cache_lin[i].used) ++lin;
    }

    TexCacheEntry tmp_pa [MAX_TEX_CACHE];
    TexCacheEntry tmp_lin[MAX_TEX_CACHE];
    for (int i = 0; i < MAX_TEX_CACHE; ++i) {
        tmp_pa [i] = s_cache_pa [i];  s_cache_pa [i] = {};
        tmp_lin[i] = s_cache_lin[i];  s_cache_lin[i] = {};
    }
    ReleaseCacheArray(tmp_pa);
    ReleaseCacheArray(tmp_lin);

    fprintf(stdout, "[TexCache] shutdown: released %d pixel-art + %d linear textures\n",
            pa, lin);
}

int MdTextureCache_Stats(int* out_pa, int* out_lin) {
    int pa = 0, lin = 0;
    for (int i = 0; i < MAX_TEX_CACHE; ++i) {
        if (s_cache_pa [i].used) ++pa;
        if (s_cache_lin[i].used) ++lin;
    }
    if (out_pa)  *out_pa  = pa;
    if (out_lin) *out_lin = lin;
    return pa + lin;
}

void MdBindTexture(MdTexture t, int unit) {
    if (t.id) {
        glActiveTexture(GL_TEXTURE0 + (unsigned int)unit);
        glBindTexture(GL_TEXTURE_2D, t.id);
    }
}

