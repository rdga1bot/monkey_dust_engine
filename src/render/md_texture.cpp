#include <monkey_dust/render/md_texture.h>

#if defined(MD_OPENGL43_ENABLED) || defined(MD_SDL_GPU)
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
    return FromGpuTexture(gt);
}

// FILTER RULES (DO NOT SIMPLIFY):
//   LINEAR_MIPMAP min + gen_mipmap: prevents GL_NEAREST vanishing at small sizes.
//   NEAREST mag: crisp pixel-art at zoom-in.
//   CLAMP_TO_EDGE: prevents atlas seam bleeding.
MdTexture MdLoadTexturePixelArt(const char* path) {
    GpuTexture gt;
    if (!gt.InitFromFile(path, GpuSamplerDesc::PixelArt())) return {};
    return FromGpuTexture(gt);
}

MdTexture MdLoadTextureFromMemory(const uint8_t* data, int w, int h) {
    GpuTexture gt;
    if (!gt.InitFromMemory(data, w, h, GpuSamplerDesc::Lut())) return {};
    return FromGpuTexture(gt);
}

void MdUnloadTexture(MdTexture& t) {
    if (t.id) { glDeleteTextures(1, &t.id); }
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        if (t.sdl_tex)     SDL_ReleaseGPUTexture(dev, (SDL_GPUTexture*)t.sdl_tex);
        if (t.sdl_sampler) SDL_ReleaseGPUSampler(dev, (SDL_GPUSampler*)t.sdl_sampler);
    }
#endif
    t = {};
}

void MdBindTexture(MdTexture t, int unit) {
    if (t.id) {
        glActiveTexture(GL_TEXTURE0 + (unsigned int)unit);
        glBindTexture(GL_TEXTURE_2D, t.id);
    }
}

#endif // MD_OPENGL43_ENABLED || MD_SDL_GPU
