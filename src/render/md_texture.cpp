#include <monkey_dust/render/md_texture.h>

#ifdef MD_OPENGL43_ENABLED
#include <monkey_dust/render/gpu_hal.h>
#include "glad.h"
#include <cstdio>

// MdTexture functions now delegate to GpuTexture + GpuSamplerDesc.
// The GL texture ID is extracted via Release() and stored in MdTexture.id.
// This keeps MdTexture as a lightweight value type while the HAL layer
// manages all GL calls.
//
// SDL_GPU future: GpuTexture::InitFromFile will call SDL_CreateGPUTexture +
// SDL_UploadToGPUTexture, and GpuSamplerDesc will become SDL_GPUSamplerCreateInfo.

MdTexture MdLoadTexture(const char* path) {
    GpuTexture gt;
    auto s = GpuSamplerDesc::Default();
    s.flip_v     = true;
    s.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    s.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    s.wrap_s     = GpuSamplerDesc::Wrap::REPEAT;
    s.wrap_t     = GpuSamplerDesc::Wrap::REPEAT;
    s.gen_mipmap = true;
    if (!gt.InitFromFile(path, s)) return {};
    return { gt.Release(), gt.Width(), gt.Height() };
}

// CONVENTION (DO NOT CHANGE without updating tile_map_renderer.cpp UV formulas):
//   flip_v=true: GL v=0 = bottom of image file, GL v=1 = top of image file.
//   Tile UV formula: v_gl = 1.0f - y_file / atlas_h
//   Removing the flip makes ALL tile UVs wrong.
//
// FILTER RULES (DO NOT SIMPLIFY):
//   LINEAR_MIPMAP min + gen_mipmap: prevents GL_NEAREST vanishing at small sizes.
//   NEAREST mag: crisp pixel-art at zoom-in.
//   CLAMP_TO_EDGE: prevents atlas seam bleeding.
MdTexture MdLoadTexturePixelArt(const char* path) {
    GpuTexture gt;
    if (!gt.InitFromFile(path, GpuSamplerDesc::PixelArt())) return {};
    return { gt.Release(), gt.Width(), gt.Height() };
}

MdTexture MdLoadTextureFromMemory(const uint8_t* data, int w, int h) {
    GpuTexture gt;
    if (!gt.InitFromMemory(data, w, h, GpuSamplerDesc::Lut())) return {};
    return { gt.Release(), gt.Width(), gt.Height() };
}

void MdUnloadTexture(MdTexture& t) {
    if (t.id) { glDeleteTextures(1, &t.id); t = {}; }
}

void MdBindTexture(MdTexture t, int unit) {
    glActiveTexture(GL_TEXTURE0 + (unsigned int)unit);
    glBindTexture(GL_TEXTURE_2D, t.id);
}

#endif
