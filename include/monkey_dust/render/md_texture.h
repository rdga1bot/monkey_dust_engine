#pragma once
// MdTexture — texture loader using stb_image (no Raylib).
// Supported backends: OpenGL 4.3 (MD_OPENGL43_ENABLED) and SDL_GPU (MD_SDL_GPU).
// Under neither flag all functions are no-ops / return {}.
//
// Implementation lives in md_texture.cpp.

#include <cstdint>

// IMPORTANT: layout must NOT change between MD_SDL_GPU and non-SDL_GPU builds.
// Engine lib is always compiled with MD_SDL_GPU; editor/tools may omit it.
// Mismatched struct sizes cause ABI breakage (wrong array offsets, bad returns).
struct MdTexture {
    unsigned int id = 0;           // OpenGL texture name (0 in SDL_GPU path)
    int w = 0, h = 0;
    void* sdl_tex     = nullptr;   // SDL_GPUTexture*  (null when not using SDL_GPU)
    void* sdl_sampler = nullptr;   // SDL_GPUSampler*  (null when not using SDL_GPU)
#ifdef MD_USE_LIBGODOT
    // RenderingServer texture RID (RID::get_id()/RID::from_uint64()), a
    // THIRD, orthogonal backend -- never built alongside MD_SDL_GPU/GL in
    // the same binary (Фаза A CONSTRAINT: separate dual-run target), so
    // this field doesn't affect the SDL_GPU vs GL ABI-parity concern above.
    uint64_t libgodot_tex_rid = 0;
#endif
};


// Load RGBA texture from file via stb_image.
// mip_lod_bias: L2 GL2TextureDetail — 0=full, +1=half-res mip, +2=quarter-res.
MdTexture MdLoadTexture(const char* path, float mip_lod_bias = 0.f);

// Load RGBA texture optimized for pixel-art rendering:
// GL_NEAREST filter + GL_CLAMP_TO_EDGE + mipmap (no seam bleeding / aliasing).
// Use for: Flare sprite atlases, tile sheets, any pixel-art content.
MdTexture MdLoadTexturePixelArt(const char* path);

// Upload raw RGBA8 pixel data (for procedural textures such as BRDF LUT).
MdTexture MdLoadTextureFromMemory(const uint8_t* data, int w, int h);

void MdUnloadTexture(MdTexture& t);
void MdBindTexture  (MdTexture t, int unit);  // OpenGL only; no-op in SDL_GPU-only builds

// ── Texture cache ─────────────────────────────────────────────────────────────
// MdLoadTexture / MdLoadTexturePixelArt cache results by FNV-1a path hash.
// Repeated loads of the same path return the cached MdTexture without hitting disk.
// MdUnloadTexture on a cached texture zeroes the caller's copy; GPU resources stay
// alive until MdTextureCache_Shutdown() is called (typically at app exit).
//
// Max capacity: 128 entries per variant (pixel-art / linear).
// Call MdTextureCache_Shutdown() once before SDL_GPU device destruction.
void MdTextureCache_Shutdown();

// Returns total number of cached entries (pixel-art + linear combined).
// Writes per-variant counts if out_pa / out_lin are non-null.
int  MdTextureCache_Stats(int* out_pa = nullptr, int* out_lin = nullptr);

