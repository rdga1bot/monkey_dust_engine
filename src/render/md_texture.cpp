#include <monkey_dust/render/md_texture.h>

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
// stb_image declarations only — implementation is already compiled into libraylib.a
#include "stb_image.h"
#include <cstdio>

MdTexture MdLoadTexture(const char* path) {
    int w, h, ch;
    // Flip vertically so (0,0) is bottom-left (OpenGL convention)
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);
    stbi_set_flip_vertically_on_load(0);  // restore for Raylib calls
    if (!data) {
        fprintf(stderr, "[MdTexture] load failed: %s\n", path);
        return {};
    }

    MdTexture t;
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return t;
}

// CONVENTION (DO NOT CHANGE without updating tile_map_renderer.cpp UV formulas):
//   stbi_set_flip_vertically_on_load(1) is active for this function.
//   Result: GL v=0 = bottom of image file, GL v=1 = top of image file.
//   Tile UV formula in tile_map_renderer.cpp:  v_gl = 1.0f - y_file / atlas_h
//   If you remove the flip, ALL tile UVs become wrong (tiles invisible).
//
// FILTER RULES (DO NOT SIMPLIFY):
//   GL_LINEAR_MIPMAP_LINEAR (min) + glGenerateMipmap — without mipmaps,
//   GL_NEAREST picks a single transparent texel at small screen sizes → tiles vanish.
//   GL_NEAREST (mag) — crisp pixel-art at zoom-in.
//   GL_CLAMP_TO_EDGE — prevents atlas seam bleeding.
MdTexture MdLoadTexturePixelArt(const char* path) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!data) {
        fprintf(stderr, "[MdTexture] pixel-art load failed: %s\n", path);
        return {};
    }

    MdTexture t;
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return t;
}

MdTexture MdLoadTextureFromMemory(const uint8_t* data, int w, int h) {
    MdTexture t;
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

void MdUnloadTexture(MdTexture& t) {
    if (t.id) { glDeleteTextures(1, &t.id); t = {}; }
}

void MdBindTexture(MdTexture t, int unit) {
    glActiveTexture(GL_TEXTURE0 + (unsigned int)unit);
    glBindTexture(GL_TEXTURE_2D, t.id);
}
#endif
