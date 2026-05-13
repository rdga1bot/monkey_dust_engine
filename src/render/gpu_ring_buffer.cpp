#include <monkey_dust/render/gpu_ring_buffer.h>

#include <monkey_dust/platform/md_log.h>

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#endif


// ── Init / Shutdown ────────────────────────────────────────────────────────────

void GpuRingBuffer::Init(uint32_t size_bytes, int binding_hint) {
    size_ = size_bytes;
    cur_  = 0;

#ifdef MD_SDL_GPU
    sdl_size_ = size_bytes;
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev && size_bytes > 0) {
        SDL_GPUBufferCreateInfo bi = {};
        bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        bi.size  = size_bytes;
        sdl_device_ = SDL_CreateGPUBuffer(dev, &bi);
        if (!sdl_device_)
            MD_LOG(MD_LOG_WARNING, "[GpuRingBuffer] SDL_CreateGPUBuffer failed: %s", SDL_GetError());

        SDL_GPUTransferBufferCreateInfo ti = {};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size  = size_bytes;
        sdl_staging_ = SDL_CreateGPUTransferBuffer(dev, &ti);
        if (!sdl_staging_)
            MD_LOG(MD_LOG_WARNING, "[GpuRingBuffer] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
    }
#endif

    (void)binding_hint;
}

void GpuRingBuffer::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_device_)  { SDL_ReleaseGPUBuffer        (dev, sdl_device_);  sdl_device_  = nullptr; }
    if (sdl_staging_) { SDL_ReleaseGPUTransferBuffer(dev, sdl_staging_); sdl_staging_ = nullptr; }
    sdl_size_ = 0;
#endif
    size_ = 0;
    cur_  = 0;
}

// ── MapWrite / Unmap / BindStorage / Advance — OpenGL ring path ───────────────

void* GpuRingBuffer::MapWrite() {
    return nullptr;
}

void GpuRingBuffer::Unmap() {
    // OpenGL coherent mapping: writes are already visible to the GPU.
    // SDL_GPU: use UnmapSDL() after MapWriteSDL().
}

void GpuRingBuffer::BindStorage(int slot) {
    (void)slot;
}

void GpuRingBuffer::Advance() {
    // SDL_GPU: cycle=true on staging/device handles versioning internally.
}


// ── SDL_GPU staging path ───────────────────────────────────────────────────────

#ifdef MD_SDL_GPU
void* GpuRingBuffer::MapWriteSDL() {
    if (!sdl_staging_) return nullptr;
    return SDL_MapGPUTransferBuffer(md::GpuDevice::Get().SDLDevice(),
                                    sdl_staging_, true /*cycle*/);
}

void GpuRingBuffer::UnmapSDL() {
    if (sdl_staging_)
        SDL_UnmapGPUTransferBuffer(md::GpuDevice::Get().SDLDevice(), sdl_staging_);
}

void GpuRingBuffer::Upload(SDL_GPUCommandBuffer* cmd) {
    if (!cmd || !sdl_device_ || !sdl_staging_) return;
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = sdl_staging_;
    src.offset          = 0;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = sdl_device_;
    dst.offset = 0;
    dst.size   = sdl_size_;
    SDL_UploadToGPUBuffer(pass, &src, &dst, true /*cycle*/);
    SDL_EndGPUCopyPass(pass);
}
#endif
