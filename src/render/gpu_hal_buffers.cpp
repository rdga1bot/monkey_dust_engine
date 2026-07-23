#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/gpu_resource_tracker.h>
#include <monkey_dust/platform/md_log.h>
#include <monkey_dust/platform/md_fs.h>
#include <monkey_dust/platform/job_system.h>
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
    info.mip_lod_bias     = s.mip_lod_bias;  // L2 GL2TextureDetail: 0=full, 1=half-res, 2=quarter
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
    md::GpuResourceTracker::Get().OnDepthTextureCreate();
#endif
    (void)shadow_border;
}

void GpuDepthTexture::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_sampler_) { SDL_ReleaseGPUSampler(dev, sdl_sampler_); sdl_sampler_ = nullptr; }
    if (sdl_tex_)     {
        SDL_ReleaseGPUTexture(dev, sdl_tex_);
        sdl_tex_ = nullptr;
        md::GpuResourceTracker::Get().OnDepthTextureDestroy();
    }
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
    SDL_GPUBufferUsageFlags usage = (target == 0x8893u)  ? SDL_GPU_BUFFERUSAGE_INDEX
                                   : (target == GPU_TARGET_STORAGE) ? SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ
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
    md::GpuResourceTracker::Get().OnBufferCreate();

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

void GpuStaticBuffer::InitEmpty(unsigned int target, uint32_t size) {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    SDL_GPUBufferUsageFlags usage = (target == 0x8893u)  ? SDL_GPU_BUFFERUSAGE_INDEX
                                   : (target == GPU_TARGET_STORAGE) ? SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ
                                   : SDL_GPU_BUFFERUSAGE_VERTEX;
    SDL_GPUBufferCreateInfo buf_info = {};
    buf_info.usage = usage;
    buf_info.size  = size;
    sdl_buf_ = SDL_CreateGPUBuffer(dev, &buf_info);
    if (!sdl_buf_)
        MD_LOG(MD_LOG_WARNING, "[GpuStaticBuffer] InitEmpty: SDL_CreateGPUBuffer failed: %s", SDL_GetError());
    else
        md::GpuResourceTracker::Get().OnBufferCreate();
#else
    (void)target; (void)size;
#endif
}

bool GpuUploadBatch::Begin(uint32_t total_bytes) {
    cursor_ = 0; total_bytes_ = total_bytes; item_count_ = 0;
#ifdef MD_SDL_GPU
    transfer_ = nullptr; map_ = nullptr;
    if (total_bytes == 0) return true;
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    SDL_GPUTransferBufferCreateInfo tbuf_info = {};
    tbuf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbuf_info.size  = total_bytes;
    transfer_ = SDL_CreateGPUTransferBuffer(dev, &tbuf_info);
    if (!transfer_) {
        MD_LOG(MD_LOG_WARNING, "[GpuUploadBatch] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }
    map_ = (uint8_t*)SDL_MapGPUTransferBuffer(dev, transfer_, false);
    if (!map_) {
        MD_LOG(MD_LOG_WARNING, "[GpuUploadBatch] SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(dev, transfer_);
        transfer_ = nullptr;
        return false;
    }
    return true;
#else
    return true;
#endif
}

void GpuUploadBatch::Add(GpuStaticBuffer& buf, unsigned int target, const void* data, uint32_t size) {
    buf.InitEmpty(target, size);
#ifdef MD_SDL_GPU
    if (!map_ || item_count_ >= MAX_ITEMS || cursor_ + size > total_bytes_) return;
    if (data && size) memcpy(map_ + cursor_, data, size);
    items_[item_count_++] = { buf.SDLBuffer(), cursor_, size };
    cursor_ += size;
#else
    (void)buf; (void)target; (void)data; (void)size;
#endif
}

void GpuUploadBatch::End() {
#ifdef MD_SDL_GPU
    if (!transfer_) return;
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    SDL_UnmapGPUTransferBuffer(dev, transfer_);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) {
        MD_LOG(MD_LOG_WARNING, "[GpuUploadBatch] AcquireCmd failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(dev, transfer_);
        transfer_ = nullptr;
        return;
    }
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    for (int i = 0; i < item_count_; ++i) {
        if (!items_[i].dst || items_[i].size == 0) continue;
        SDL_GPUTransferBufferLocation src = {};
        src.transfer_buffer = transfer_;
        src.offset          = items_[i].offset;
        SDL_GPUBufferRegion dst = {};
        dst.buffer = items_[i].dst;
        dst.offset = 0;
        dst.size   = items_[i].size;
        SDL_UploadToGPUBuffer(pass, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(pass);
    if (!SDL_SubmitGPUCommandBuffer(cmd))
        MD_LOG(MD_LOG_WARNING, "[GpuUploadBatch] submit failed: %s", SDL_GetError());

    SDL_ReleaseGPUTransferBuffer(dev, transfer_);
    transfer_ = nullptr;
#endif
}

void GpuStaticBuffer::Shutdown() {
#ifdef MD_SDL_GPU
    if (sdl_buf_) {
        SDL_ReleaseGPUBuffer(md::GpuDevice::Get().SDLDevice(), sdl_buf_);
        sdl_buf_ = nullptr;
        md::GpuResourceTracker::Get().OnBufferDestroy();
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

bool GpuTexture::InitRenderTarget(int w, int h, const GpuSamplerDesc& s) {
    w_ = w; h_ = h;
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (!dev) return false;
    uint32_t num_levels = s.gen_mipmap ? MipLevels(w, h) : 1u;

    SDL_GPUTextureCreateInfo ti = {};
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width                = (Uint32)w;
    ti.height               = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = num_levels;
    sdl_tex_ = SDL_CreateGPUTexture(dev, &ti);
    if (!sdl_tex_) {
        MD_LOG(MD_LOG_WARNING, "[GpuTexture] InitRenderTarget SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return false;
    }
    md::GpuResourceTracker::Get().OnTextureCreate();
    sdl_sampler_ = CreateSDLSampler(dev, s);
    return true;
#else
    return false;
#endif
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
        md::GpuResourceTracker::Get().OnTextureCreate();

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

#ifdef MD_SDL_GPU
// ── DDS (BC1/DXT1 or BC3/DXT5) 2D texture array loader ───────────────────────

// Returns height(12), width(16), mip_count(28), is_bc3/is_bc1, data_offset.
// BC1 support added for Kenshi's terrain _NML.dds files, which are DXT1 (not
// DXT5 like the _DIF.dds diffuse array) -- confirmed via FourCC byte.
static bool s_parse_dds(const uint8_t* buf, uint32_t len,
                         int& w, int& h, int& mips, bool& is_bc3, bool& is_bc1, uint32_t& data_off)
{
    if (len < 128 || buf[0]!='D'||buf[1]!='D'||buf[2]!='S'||buf[3]!=' ') return false;
    auto r32 = [&](uint32_t o) { uint32_t v; memcpy(&v, buf+o, 4); return v; };
    h       = (int)r32(12);
    w       = (int)r32(16);
    mips    = (int)r32(28); if (mips < 1) mips = 1;
    // DDPIXELFORMAT at offset 76: flags(+4), fourCC(+8)
    uint32_t pf_flags = r32(80);
    uint32_t fourcc   = r32(84);
    is_bc3   = (pf_flags & 4u) && (fourcc == 0x35545844u); // DDPF_FOURCC && 'DXT5'
    is_bc1   = (pf_flags & 4u) && (fourcc == 0x31545844u); // DDPF_FOURCC && 'DXT1'
    data_off = 128;
    return true;
}

static size_t s_bc_mip_bytes(int w, int h, int bytes_per_block)
{
    int bw = (w+3)/4, bh = (h+3)/4;
    return (size_t)bw * bh * (size_t)bytes_per_block;
}

bool GpuTexture::InitFromDDSArray(const char* const* paths, int count,
                                   const GpuSamplerDesc& /*s*/)
{
    // Note: the passed sampler desc's mip fields are intentionally not used
    // here (unlike CreateSDLSampler(), which gates max_lod on gen_mipmap) —
    // this loader always uploads a full real mip chain straight from the DDS
    // file's own mips, so the sampler below hardcodes max_lod=ref_mips-1
    // (the correct range for that chain) regardless of the caller's
    // gen_mipmap flag. Verified via GPU debug: forcing max_lod=0 here (mip0
    // only) did not change the render, confirming mip selection was never
    // the issue in this array's sampling path.
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (!dev || count < 1 || !paths) return false;

    struct LayerData { char* buf; uint32_t len; const char* path; };
    LayerData* layers = (LayerData*)malloc((size_t)count * sizeof(LayerData));
    if (!layers) return false;
    memset(layers, 0, (size_t)count * sizeof(LayerData));
    for (int i = 0; i < count; ++i) layers[i].path = paths[i];

    // Parallel file read — each layer's fs_read_alloc touches only its own
    // buffer (no GPU calls, no MdRegistry), matching JobSystem's documented
    // safe-usage contract exactly (see job_system.h). Confirmed by direct
    // measurement this loop (previously one fopen/fread/fclose per layer in
    // series, up to 125 layers) was a real contributor to startup stalls
    // alongside the JoltWorld::AddTerrainMesh fix — reading independent files
    // in parallel across JobSystem's workers cuts wall-clock read time
    // roughly by the worker count on multi-core hardware. Falls back to
    // serial reads if JobSystem has no workers (e.g. test binaries that
    // never call JobSystem::Init()).
    auto read_one = [](void* p) { auto* l = (LayerData*)p; l->buf = md::fs_read_alloc(l->path, &l->len); };
    if (JobSystem::Get().NumWorkers() > 0) {
        for (int i = 0; i < count; ++i) JobSystem::Get().Submit(read_one, &layers[i]);
        JobSystem::Get().Flush();
    } else {
        for (int i = 0; i < count; ++i) read_one(&layers[i]);
    }

    int ref_w = 0, ref_h = 0, ref_mips = 0; bool ref_bc3 = false, ref_bc1 = false;
    uint32_t ref_doff = 0;
    bool ok = true;

    for (int i = 0; i < count && ok; ++i) {
        if (!layers[i].buf) {
            fprintf(stderr, "[GpuTexture] DDS array: missing (i=%d) '%s'\n", i, paths[i] ? paths[i] : "(null)");
            MD_LOG(MD_LOG_WARNING, "[GpuTexture] DDS array: missing %s", paths[i]);
            ok = false; break;
        }
        int w, h, m; bool bc3, bc1; uint32_t doff;
        if (!s_parse_dds((const uint8_t*)layers[i].buf, layers[i].len, w, h, m, bc3, bc1, doff)) {
            fprintf(stderr, "[GpuTexture] DDS array: bad header (i=%d) '%s'\n", i, paths[i]);
            MD_LOG(MD_LOG_WARNING, "[GpuTexture] DDS array: bad header %s", paths[i]);
            ok = false; break;
        }
        if (i == 0) { ref_w=w; ref_h=h; ref_mips=m; ref_bc3=bc3; ref_bc1=bc1; ref_doff=doff; }
        else if (w!=ref_w||h!=ref_h||m!=ref_mips||bc3!=ref_bc3||bc1!=ref_bc1) {
            fprintf(stderr, "[GpuTexture] DDS array: size mismatch (i=%d) '%s' w=%d h=%d m=%d bc3=%d bc1=%d vs ref w=%d h=%d m=%d bc3=%d bc1=%d\n",
                    i, paths[i], w, h, m, bc3, bc1, ref_w, ref_h, ref_mips, ref_bc3, ref_bc1);
            MD_LOG(MD_LOG_WARNING, "[GpuTexture] DDS array: layer %d size mismatch", i);
            ok = false; break;
        }
    }
    if (ok && !ref_bc3 && !ref_bc1) {
        MD_LOG(MD_LOG_WARNING, "[GpuTexture] DDS array: only BC1/DXT1 or BC3/DXT5 supported");
        ok = false;
    }

    if (!ok) {
        for (int i=0;i<count;++i) md::fs_free(layers[i].buf);
        free(layers); return false;
    }

    // Compute per-mip byte sizes and total transfer size
    const int block_bytes = ref_bc3 ? 16 : 8;  // BC3=16B/block, BC1=8B/block
    size_t mip_sz[32] = {};
    size_t layer_stride = 0;
    { int mw=ref_w, mh=ref_h;
      for (int m=0; m<ref_mips && m<32; ++m) {
          mip_sz[m] = s_bc_mip_bytes(mw, mh, block_bytes);
          layer_stride += mip_sz[m];
          mw = mw>1?mw/2:1; mh = mh>1?mh/2:1;
      }
    }
    uint32_t total = (uint32_t)(layer_stride * (size_t)count);

    SDL_GPUTextureCreateInfo ti = {};
    ti.type                 = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    ti.format               = ref_bc3 ? SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM
                                       : SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width                = (Uint32)ref_w;
    ti.height               = (Uint32)ref_h;
    ti.layer_count_or_depth = (Uint32)count;
    ti.num_levels           = (Uint32)ref_mips;
    fprintf(stderr, "[DDS array] creating texture %dx%d %d layers %d mips, transfer=%u bytes\n",
            ref_w, ref_h, count, ref_mips, total);
    sdl_tex_ = SDL_CreateGPUTexture(dev, &ti);
    if (!sdl_tex_) {
        MD_LOG(MD_LOG_WARNING, "[GpuTexture] DDS array: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        for (int i=0;i<count;++i) md::fs_free(layers[i].buf);
        free(layers); return false;
    }
    md::GpuResourceTracker::Get().OnTextureCreate();
    fprintf(stderr, "[DDS array] texture created OK\n");

    SDL_GPUTransferBufferCreateInfo tbci = {};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size  = total;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbci);
    if (!tb) {
        SDL_ReleaseGPUTexture(dev, sdl_tex_); sdl_tex_ = nullptr;
        md::GpuResourceTracker::Get().OnTextureDestroy();
        for (int i=0;i<count;++i) md::fs_free(layers[i].buf);
        free(layers); return false;
    }
    fprintf(stderr, "[DDS array] transfer buffer created OK\n");

    uint8_t* mapped = (uint8_t*)SDL_MapGPUTransferBuffer(dev, tb, false);
    if (mapped) {
        uint32_t dst_off = 0;
        for (int i=0;i<count;++i) {
            memcpy(mapped + dst_off, (const uint8_t*)layers[i].buf + ref_doff, layer_stride);
            dst_off += (uint32_t)layer_stride;
        }
        SDL_UnmapGPUTransferBuffer(dev, tb);
    }
    fprintf(stderr, "[DDS array] data mapped+copied OK\n");

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) {
        fprintf(stderr, "[DDS array] AcquireGPUCommandBuffer returned NULL!\n");
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        SDL_ReleaseGPUTexture(dev, sdl_tex_); sdl_tex_ = nullptr;
        md::GpuResourceTracker::Get().OnTextureDestroy();
        for (int i=0;i<count;++i) md::fs_free(layers[i].buf);
        free(layers); return false;
    }
    fprintf(stderr, "[DDS array] cmd acquired OK\n");
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    uint32_t tb_off = 0;
    for (int i=0;i<count;++i) {
        int mw=ref_w, mh=ref_h;
        for (int m=0; m<ref_mips && m<32; ++m) {
            SDL_GPUTextureTransferInfo src = {};
            src.transfer_buffer = tb;
            src.offset          = tb_off;
            src.pixels_per_row  = (Uint32)mw;
            src.rows_per_layer  = (Uint32)mh;
            SDL_GPUTextureRegion dst = {};
            dst.texture   = sdl_tex_;
            dst.mip_level = (Uint32)m;
            dst.layer     = (Uint32)i;
            dst.w         = (Uint32)mw;
            dst.h         = (Uint32)mh;
            dst.d         = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, false);
            tb_off += (uint32_t)mip_sz[m];
            mw = mw>1?mw/2:1; mh = mh>1?mh/2:1;
        }
    }

    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);

    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_LINEAR;
    si.mag_filter     = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.min_lod        = 0.f;
    si.max_lod        = (float)(ref_mips - 1);
    sdl_sampler_ = SDL_CreateGPUSampler(dev, &si);

    w_ = ref_w; h_ = ref_h;
    for (int i=0;i<count;++i) md::fs_free(layers[i].buf);
    free(layers);

    if (!sdl_sampler_) {
        SDL_ReleaseGPUTexture(dev, sdl_tex_); sdl_tex_ = nullptr;
        md::GpuResourceTracker::Get().OnTextureDestroy();
        return false;
    }
    return true;
}
#endif // MD_SDL_GPU (InitFromDDSArray)

void GpuTexture::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_sampler_) { SDL_ReleaseGPUSampler(dev, sdl_sampler_); sdl_sampler_ = nullptr; }
    if (sdl_tex_) {
        SDL_ReleaseGPUTexture(dev, sdl_tex_);
        sdl_tex_ = nullptr;
        md::GpuResourceTracker::Get().OnTextureDestroy();
    }
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
