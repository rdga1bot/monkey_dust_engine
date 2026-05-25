#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/platform/md_log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "glad.h"
#include "stb_image.h"

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#endif

// ── SDL_GPU texture helpers ────────────────────────────────────────────────────

#ifdef MD_SDL_GPU

static uint32_t MipLevels(int w, int h) {
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

static SDL_GPUSampler* CreateSDLSampler(SDL_GPUDevice* dev, const GpuSamplerDesc& s) {
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
    return SDL_CreateGPUSampler(dev, &info);
}

#endif // MD_SDL_GPU

// ── GpuVertexBuffer ───────────────────────────────────────────────────────────

void GpuVertexBuffer::Init(uint32_t max_vertices, uint32_t vertex_stride) {
    stride_ = vertex_stride;
#ifdef MD_SDL_GPU
    sdl_size_ = max_vertices * vertex_stride;
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();

    SDL_GPUBufferCreateInfo buf_info = {};
    buf_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buf_info.size  = sdl_size_;
    sdl_buf_ = SDL_CreateGPUBuffer(dev, &buf_info);
    if (!sdl_buf_)
        MD_LOG(MD_LOG_WARNING, "[GpuVertexBuffer] SDL_CreateGPUBuffer failed: %s", SDL_GetError());

    SDL_GPUTransferBufferCreateInfo tbuf_info = {};
    tbuf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbuf_info.size  = sdl_size_;
    sdl_transfer_ = SDL_CreateGPUTransferBuffer(dev, &tbuf_info);
    if (!sdl_transfer_)
        MD_LOG(MD_LOG_WARNING, "[GpuVertexBuffer] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
#endif
}

void GpuVertexBuffer::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_buf_)      { SDL_ReleaseGPUBuffer(dev, sdl_buf_);                sdl_buf_      = nullptr; }
    if (sdl_transfer_) { SDL_ReleaseGPUTransferBuffer(dev, sdl_transfer_);   sdl_transfer_ = nullptr; }
    sdl_size_ = 0;
#endif
    stride_ = 0;
}

void* GpuVertexBuffer::MapWrite() {
#ifdef MD_SDL_GPU
    if (!sdl_transfer_) return nullptr;
    return SDL_MapGPUTransferBuffer(md::GpuDevice::Get().SDLDevice(),
                                    sdl_transfer_, true /*cycle*/);
#else
    return nullptr;
#endif
}

void GpuVertexBuffer::Unmap() {
#ifdef MD_SDL_GPU
    if (sdl_transfer_)
        SDL_UnmapGPUTransferBuffer(md::GpuDevice::Get().SDLDevice(), sdl_transfer_);
#endif
}

#ifdef MD_SDL_GPU
void GpuVertexBuffer::Upload(SDL_GPUCommandBuffer* cmd) {
    if (!cmd || !sdl_buf_ || !sdl_transfer_) return;
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = sdl_transfer_;
    src.offset          = 0;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = sdl_buf_;
    dst.offset = 0;
    dst.size   = sdl_size_;
    SDL_UploadToGPUBuffer(pass, &src, &dst, true /*cycle*/);
    SDL_EndGPUCopyPass(pass);
}
#endif

void GpuVertexBuffer::Advance() {
    // SDL_GPU: cycle=true in MapWrite/Upload handles versioning — no explicit advance needed.
}

// ── GpuDepthTexture ───────────────────────────────────────────────────────────

void GpuDepthTexture::Init(int w, int h, bool shadow_border) {
    w_ = w; h_ = h;
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();

    SDL_GPUTextureCreateInfo ti = {};
    ti.type                  = SDL_GPU_TEXTURETYPE_2D;
    // D32_SFLOAT is universally supported for SAMPLER usage in Vulkan (D24_UNORM is not on Intel Gen9).
    ti.format                = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ti.usage                 = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                               SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width                 = (Uint32)w;
    ti.height                = (Uint32)h;
    ti.layer_count_or_depth  = 1;
    ti.num_levels            = 1;
    sdl_tex_ = SDL_CreateGPUTexture(dev, &ti);
    if (!sdl_tex_) {
        MD_LOG(MD_LOG_WARNING, "[GpuDepthTexture] SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return;
    }
    // SDL3 has no CLAMP_TO_BORDER — use CLAMP_TO_EDGE (minor shadow-edge artefact).
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_LINEAR;
    si.mag_filter     = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.min_lod        = 0.0f;
    si.max_lod        = 0.0f;
    sdl_sampler_ = SDL_CreateGPUSampler(dev, &si);
#endif
    (void)shadow_border;
}

void GpuDepthTexture::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_sampler_) { SDL_ReleaseGPUSampler(dev, sdl_sampler_); sdl_sampler_ = nullptr; }
    if (sdl_tex_)     { SDL_ReleaseGPUTexture(dev, sdl_tex_);     sdl_tex_     = nullptr; }
#endif
    w_ = h_ = 0;
}

void GpuDepthTexture::Bind(uint32_t unit) const {
    (void)unit; // SDL_GPU: binding via SDL_BindGPUFragmentSamplers in render pass (Step 6)
}

// ── GpuStaticBuffer ───────────────────────────────────────────────────────────

void GpuStaticBuffer::Init(unsigned int target, const void* data, uint32_t size) {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();

    // Map GL target → SDL_GPU buffer usage.
    // 0x8893 = GL_ELEMENT_ARRAY_BUFFER value (avoid GL header dependency in SDL-only builds).
    SDL_GPUBufferUsageFlags usage = (target == 0x8893u)
                                    ? SDL_GPU_BUFFERUSAGE_INDEX
                                    : SDL_GPU_BUFFERUSAGE_VERTEX;
    (void)target;

    SDL_GPUBufferCreateInfo buf_info = {};
    buf_info.usage = usage;
    buf_info.size  = size;
    sdl_buf_ = SDL_CreateGPUBuffer(dev, &buf_info);
    if (!sdl_buf_) {
        MD_LOG(MD_LOG_WARNING, "[GpuStaticBuffer] SDL_CreateGPUBuffer failed: %s", SDL_GetError());
        return;
    }

    // One-shot upload: staging transfer buffer → device buffer.
    SDL_GPUTransferBufferCreateInfo tbuf_info = {};
    tbuf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbuf_info.size  = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(dev, &tbuf_info);
    if (!transfer) {
        MD_LOG(MD_LOG_WARNING, "[GpuStaticBuffer] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return;
    }

    void* map = SDL_MapGPUTransferBuffer(dev, transfer, false);
    if (map) { memcpy(map, data, size); SDL_UnmapGPUTransferBuffer(dev, transfer); }

    SDL_GPUCommandBuffer* cmd  = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) { MD_LOG(MD_LOG_WARNING, "[GpuStaticBuffer] AcquireCmd failed: %s", SDL_GetError()); return; }
    SDL_GPUCopyPass*      pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = transfer;
    src.offset          = 0;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = sdl_buf_;
    dst.offset = 0;
    dst.size   = size;
    SDL_UploadToGPUBuffer(pass, &src, &dst, false /*no cycle — one shot*/);
    SDL_EndGPUCopyPass(pass);
    if (!SDL_SubmitGPUCommandBuffer(cmd))
        MD_LOG(MD_LOG_WARNING, "[GpuStaticBuffer] submit failed: %s", SDL_GetError());

    SDL_ReleaseGPUTransferBuffer(dev, transfer); // staging no longer needed
#endif

}

void GpuStaticBuffer::Shutdown() {
#ifdef MD_SDL_GPU
    if (sdl_buf_) {
        SDL_ReleaseGPUBuffer(md::GpuDevice::Get().SDLDevice(), sdl_buf_);
        sdl_buf_ = nullptr;
    }
#endif
}

void GpuStaticBuffer::Bind(unsigned int target) const {
    (void)target;
}

void GpuStaticBuffer::BindVertex(uint32_t slot, uint32_t stride, uint64_t offset) const {
    (void)slot; (void)stride; (void)offset;
}

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
    int ch;
    stbi_set_flip_vertically_on_load(s.flip_v ? 1 : 0);
    uint8_t* data = stbi_load(path, &w_, &h_, &ch, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!data) { fprintf(stderr, "[GpuTexture] load failed: %s\n", path); return false; }
    bool ok = InitFromMemory(data, w_, h_, s);
    stbi_image_free(data);
    return ok;
}

bool GpuTexture::InitFromMemory(const uint8_t* rgba8, int w, int h, const GpuSamplerDesc& s) {
    w_ = w; h_ = h;
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        uint32_t num_levels = s.gen_mipmap ? MipLevels(w, h) : 1u;
        SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (s.gen_mipmap) usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;

        SDL_GPUTextureCreateInfo ti = {};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage                = usage;
        ti.width                = (Uint32)w;
        ti.height               = (Uint32)h;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = num_levels;
        sdl_tex_ = SDL_CreateGPUTexture(dev, &ti);
        if (!sdl_tex_) {
            MD_LOG(MD_LOG_WARNING, "[GpuTexture] SDL_CreateGPUTexture failed: %s", SDL_GetError());
            return false;
        }

        uint32_t upload_size = (uint32_t)(w * h * 4);
        SDL_GPUTransferBufferCreateInfo tbuf = {};
        tbuf.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbuf.size  = upload_size;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(dev, &tbuf);
        void* map = SDL_MapGPUTransferBuffer(dev, transfer, false);
        if (map) { memcpy(map, rgba8, upload_size); SDL_UnmapGPUTransferBuffer(dev, transfer); }

        SDL_GPUCommandBuffer* cmd  = SDL_AcquireGPUCommandBuffer(dev);
        if (!cmd) { MD_LOG(MD_LOG_WARNING, "[GpuTexture] AcquireCmd failed: %s", SDL_GetError()); SDL_ReleaseGPUTransferBuffer(dev, transfer); return false; }
        SDL_GPUCopyPass*      pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src_info = {};
        src_info.transfer_buffer = transfer;
        src_info.pixels_per_row  = (Uint32)w;
        src_info.rows_per_layer  = (Uint32)h;
        SDL_GPUTextureRegion dst_region = {};
        dst_region.texture = sdl_tex_;
        dst_region.w       = (Uint32)w;
        dst_region.h       = (Uint32)h;
        dst_region.d       = 1;
        SDL_UploadToGPUTexture(pass, &src_info, &dst_region, false);
        SDL_EndGPUCopyPass(pass);
        if (s.gen_mipmap) SDL_GenerateMipmapsForGPUTexture(cmd, sdl_tex_);
        if (!SDL_SubmitGPUCommandBuffer(cmd))
            MD_LOG(MD_LOG_WARNING, "[GpuTexture] submit failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(dev, transfer);

        sdl_sampler_ = CreateSDLSampler(dev, s);
        return true;
    }
    // dev == null: SDL_GPU not initialised — fall through to OpenGL path below.
#endif
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
    ApplySampler(s);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void GpuTexture::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_sampler_) { SDL_ReleaseGPUSampler(dev, sdl_sampler_); sdl_sampler_ = nullptr; }
    if (sdl_tex_)     { SDL_ReleaseGPUTexture(dev, sdl_tex_);     sdl_tex_     = nullptr; }
#endif
#ifndef MD_SDL_GPU
    if (id_) { glDeleteTextures(1, &id_); id_ = 0; }
#endif
    w_ = h_ = 0;
}

void GpuTexture::Bind(uint32_t unit) const {
#ifndef MD_SDL_GPU
    if (id_) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, id_);
    }
#else
    (void)unit;
#endif
}
