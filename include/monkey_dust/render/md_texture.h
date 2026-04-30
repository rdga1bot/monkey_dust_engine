#pragma once
// MdTexture — texture loader using stb_image (no Raylib).
// Under !MD_OPENGL43_ENABLED all functions are no-ops / return {}.
//
// Implementation lives in MdTexture.cpp; stbi symbols come from libraylib.a.

#include <cstdint>

struct MdTexture { unsigned int id = 0; int w = 0, h = 0; };

#ifdef MD_OPENGL43_ENABLED

// Load RGBA texture from file via stb_image.
MdTexture MdLoadTexture(const char* path);

// Load RGBA texture optimized for pixel-art rendering:
// GL_NEAREST filter (no smoothing) + GL_CLAMP_TO_EDGE wrap (no seam bleeding).
// Use for: Flare sprite atlases, tile sheets, any pixel-art content.
MdTexture MdLoadTexturePixelArt(const char* path);

// Upload raw RGBA8 pixel data (for procedural textures such as BRDF LUT).
MdTexture MdLoadTextureFromMemory(const uint8_t* data, int w, int h);

void MdUnloadTexture(MdTexture& t);
void MdBindTexture  (MdTexture t, int unit);

#else
inline MdTexture MdLoadTexture(const char*)                        { return {}; }
inline MdTexture MdLoadTexturePixelArt(const char*)                { return {}; }
inline MdTexture MdLoadTextureFromMemory(const uint8_t*, int, int) { return {}; }
inline void      MdUnloadTexture(MdTexture&)                       {}
inline void      MdBindTexture  (MdTexture, int)                   {}
#endif
