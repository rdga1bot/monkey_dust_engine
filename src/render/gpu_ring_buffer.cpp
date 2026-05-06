#include <monkey_dust/render/gpu_ring_buffer.h>

#if defined(MD_SDL_GPU) || defined(MD_OPENGL43_ENABLED)
#include <monkey_dust/platform/md_log.h>
#endif

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#endif

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#include <cstdio>

static inline GLsync& as_sync(void*& p) { return reinterpret_cast<GLsync&>(p); }
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

#ifdef MD_OPENGL43_ENABLED
    binding_  = binding_hint;
    base_ptr_ = nullptr;
    if (size_ == 0) {
        MD_LOG(MD_LOG_WARNING, "[GpuRingBuffer] Init: size_bytes == 0");
        return;
    }
    glGenBuffers(1, &gl_buf_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_buf_);
    const GLbitfield storage_flags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
        GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT;
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    (GLsizeiptr)size_ * N_FRAMES, nullptr, storage_flags);
    base_ptr_ = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                                 (GLsizeiptr)size_ * N_FRAMES,
                                 GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
                                 GL_MAP_COHERENT_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    if (!base_ptr_)
        MD_LOG(MD_LOG_WARNING, "[GpuRingBuffer] glMapBufferRange failed for %u bytes",
               size_ * N_FRAMES);
#else
    (void)binding_hint;
#endif
}

void GpuRingBuffer::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_device_)  { SDL_ReleaseGPUBuffer        (dev, sdl_device_);  sdl_device_  = nullptr; }
    if (sdl_staging_) { SDL_ReleaseGPUTransferBuffer(dev, sdl_staging_); sdl_staging_ = nullptr; }
    sdl_size_ = 0;
#endif
#ifdef MD_OPENGL43_ENABLED
    if (gl_buf_) {
        for (int i = 0; i < N_FRAMES; ++i) {
            if (as_sync(fences_[i])) { glDeleteSync(as_sync(fences_[i])); fences_[i] = nullptr; }
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_buf_);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glDeleteBuffers(1, &gl_buf_);
        gl_buf_ = 0;
        base_ptr_ = nullptr;
    }
#endif
    size_ = 0;
    cur_  = 0;
}

// ── MapWrite / Unmap / BindStorage / Advance — OpenGL ring path ───────────────

void* GpuRingBuffer::MapWrite() {
#ifdef MD_OPENGL43_ENABLED
    if (!base_ptr_) return nullptr;
    WaitFence(cur_);
    return static_cast<uint8_t*>(base_ptr_) + (size_t)cur_ * size_;
#else
    return nullptr;
#endif
}

void GpuRingBuffer::Unmap() {
    // OpenGL coherent mapping: writes are already visible to the GPU.
    // SDL_GPU: use UnmapSDL() after MapWriteSDL().
}

void GpuRingBuffer::BindStorage(int slot) {
#ifdef MD_OPENGL43_ENABLED
    if (!gl_buf_) return;
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, (GLuint)slot, gl_buf_,
                      (GLintptr)cur_ * size_, size_);
#else
    (void)slot;
#endif
}

void GpuRingBuffer::Advance() {
#ifdef MD_OPENGL43_ENABLED
    if (!gl_buf_) return;
    GLsync& fence = as_sync(fences_[cur_]);
    if (fence) glDeleteSync(fence);
    fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    cur_ = (cur_ + 1) % N_FRAMES;
#endif
    // SDL_GPU: cycle=true on staging/device handles versioning internally.
}

#ifdef MD_OPENGL43_ENABLED
void GpuRingBuffer::WaitFence(int slot) {
    GLsync& fence = as_sync(fences_[slot]);
    if (!fence) return;
    GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 10'000'000);
    if (result == GL_TIMEOUT_EXPIRED || result == GL_WAIT_FAILED)
        MD_LOG(MD_LOG_WARNING, "[GpuRingBuffer] fence wait timeout on slot %d", slot);
    glDeleteSync(fence);
    fence = nullptr;
}
#endif

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
