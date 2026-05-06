#pragma once
// MdTexture — texture loader using stb_image (no Raylib).
// Supported backends: OpenGL 4.3 (MD_OPENGL43_ENABLED) and SDL_GPU (MD_SDL_GPU).
// Under neither flag all functions are no-ops / return {}.
//
// Implementation lives in md_texture.cpp.

#include <cstdint>

struct MdTexture {
    unsigned int id = 0;   // OpenGL texture name (0 in SDL_GPU-only path)
    int w = 0, h = 0;
#ifdef MD_SDL_GPU
    void* sdl_tex     = nullptr;  // SDL_GPUTexture* (opaque to avoid SDL header in every TU)
    void* sdl_sampler = nullptr;  // SDL_GPUSampler*
#endif
};

#if defined(MD_OPENGL43_ENABLED) || defined(MD_SDL_GPU)

// Load RGBA texture from file via stb_image.
MdTexture MdLoadTexture(const char* path);

// Load RGBA texture optimized for pixel-art rendering:
// GL_NEAREST filter + GL_CLAMP_TO_EDGE + mipmap (no seam bleeding / aliasing).
// Use for: Flare sprite atlases, tile sheets, any pixel-art content.
MdTexture MdLoadTexturePixelArt(const char* path);

// Upload raw RGBA8 pixel data (for procedural textures such as BRDF LUT).
MdTexture MdLoadTextureFromMemory(const uint8_t* data, int w, int h);

void MdUnloadTexture(MdTexture& t);
void MdBindTexture  (MdTexture t, int unit);  // OpenGL only; no-op in SDL_GPU-only builds

#else
inline MdTexture MdLoadTexture(const char*)                        { return {}; }
inline MdTexture MdLoadTexturePixelArt(const char*)                { return {}; }
inline MdTexture MdLoadTextureFromMemory(const uint8_t*, int, int) { return {}; }
inline void      MdUnloadTexture(MdTexture&)                       {}
inline void      MdBindTexture  (MdTexture, int)                   {}
#endif
