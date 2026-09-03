#include <monkey_dust/render/md_texture.h>

#include <cstdio>
#include <monkey_dust/render/gpu_hal.h>
#include "glad.h"
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <SDL3/SDL_gpu.h>
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
}

static void ReleaseCacheArray(TexCacheEntry* c) {
    for (int i = 0; i < MAX_TEX_CACHE; ++i) {
        if (!c[i].used) continue;
        MdUnloadTexture(c[i].tex);   // safe: not in cache during Shutdown
        c[i] = {};
    }
}

// ─────────────────────────────────────────────────────────────────────────────

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

    GpuTexture gt;
    if (!gt.InitFromFile(path, GpuSamplerDesc::PixelArt())) return {};
    MdTexture t = FromGpuTexture(gt);
    CacheInsert(s_cache_pa, h, t);
    return t;
}

MdTexture MdLoadTextureFromMemory(const uint8_t* data, int w, int h) {
    GpuTexture gt;
    if (!gt.InitFromMemory(data, w, h, GpuSamplerDesc::Lut())) return {};
    return FromGpuTexture(gt);
}

void MdUnloadTexture(MdTexture& t) {
    // If cache owns this texture, only zero the caller's copy.
    // GPU resources stay alive until MdTextureCache_Shutdown().
    if (CacheOwns(t)) { t = {}; return; }

    if (t.id) { glDeleteTextures(1, &t.id); }
#ifdef MD_SDL_GPU
    md::GpuDeviceHandle dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        if (t.sdl_tex)     GpuReleaseTexture(dev, (md::GpuTextureHandle)t.sdl_tex);
        if (t.sdl_sampler) GpuReleaseSampler(dev, (SDL_GPUSampler*)t.sdl_sampler);
    }
#endif
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
