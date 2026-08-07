#include "gpu_hal_buffers_internal.h"

// ── SDL_GPU texture helpers ────────────────────────────────────────────────────
// (external linkage — declared in gpu_hal_buffers_internal.h, shared with
// gpu_hal_buffers_dds.cpp and gpu_hal_buffers_texture_upload.cpp)

#ifdef MD_SDL_GPU

uint32_t MipLevels(int w, int h) {
    uint32_t n = 1;
    int dim = (w > h) ? w : h;
    while (dim > 1) { dim >>= 1; ++n; }
    return n;
}

static SDL_GPUFilter ToSDLFilter(GpuSamplerDesc::Filter f) {
    return (f == GpuSamplerDesc::Filter::NEAREST) ? SDL_GPU_FILTER_NEAREST
                                                   : SDL_GPU_FILTER_LINEAR;
}

static SDL_GPUSamplerAddressMode ToSDLWrap(GpuSamplerDesc::Wrap w) {
    return (w == GpuSamplerDesc::Wrap::CLAMP_TO_EDGE) ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                       : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
}

SDL_GPUSampler* CreateSDLSampler(SDL_GPUDevice* dev, const GpuSamplerDesc& s) {
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter       = ToSDLFilter(s.min_filter);
    info.mag_filter       = ToSDLFilter(s.mag_filter);
    info.mipmap_mode      = (s.min_filter == GpuSamplerDesc::Filter::LINEAR_MIPMAP)
                            ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                            : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u   = ToSDLWrap(s.wrap_s);
    info.address_mode_v   = ToSDLWrap(s.wrap_t);
    info.address_mode_w   = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.min_lod          = 0.0f;
    info.max_lod          = s.gen_mipmap ? 1000.0f : 0.0f;
    info.mip_lod_bias     = s.mip_lod_bias;  // L2 GL2TextureDetail: 0=full, 1=half-res, 2=quarter
    return SDL_CreateGPUSampler(dev, &info);
}

#endif // MD_SDL_GPU

// ── GpuTexture ────────────────────────────────────────────────────────────────

#ifndef MD_SDL_GPU
static GLenum ToGLFilter(GpuSamplerDesc::Filter f, bool is_min) {
    switch (f) {
    case GpuSamplerDesc::Filter::NEAREST:       return GL_NEAREST;
    case GpuSamplerDesc::Filter::LINEAR:        return GL_LINEAR;
    case GpuSamplerDesc::Filter::LINEAR_MIPMAP: return is_min ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    }
    return GL_LINEAR;
}

static GLenum ToGLWrap(GpuSamplerDesc::Wrap w) {
    return (w == GpuSamplerDesc::Wrap::CLAMP_TO_EDGE) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
}
#endif // !MD_SDL_GPU

void GpuTexture::ApplySampler(const GpuSamplerDesc& s) const {
#ifndef MD_SDL_GPU
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)ToGLFilter(s.min_filter, true));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)ToGLFilter(s.mag_filter, false));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     (GLint)ToGLWrap(s.wrap_s));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     (GLint)ToGLWrap(s.wrap_t));
    if (s.gen_mipmap) glGenerateMipmap(GL_TEXTURE_2D);
#else
    (void)s;  // sampler state baked into SDL_GPUSampler at creation
#endif
}

bool GpuTexture::InitFromFile(const char* path, const GpuSamplerDesc& s) {
    size_t len = strlen(path);
    if (len > 4 && strcmp(path + len - 4, ".dds") == 0) {
        return InitFromDDS(path, s);
    }
    int ch;
    stbi_set_flip_vertically_on_load(s.flip_v ? 1 : 0);
    uint8_t* data = stbi_load(path, &w_, &h_, &ch, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!data) { fprintf(stderr, "[GpuTexture] load failed: %s\n", path); return false; }
    bool ok = InitFromMemory(data, w_, h_, s);
    stbi_image_free(data);
    return ok;
}
