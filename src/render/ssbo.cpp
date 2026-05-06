#include <monkey_dust/render/ssbo.h>
#include <cstdio>
#include <cstring>

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/md_log.h>
#endif

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#endif

void SSBO::Init(int capacity_bytes, uint32_t extra_sdl_usage) {
#ifdef MD_SDL_GPU
    sdl_cap_ = (uint32_t)capacity_bytes;
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev && capacity_bytes > 0) {
        SDL_GPUBufferCreateInfo bi = {};
        bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                   SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                   extra_sdl_usage;
        bi.size  = (Uint32)capacity_bytes;
        sdl_buf_ = SDL_CreateGPUBuffer(dev, &bi);
        if (!sdl_buf_)
            MD_LOG(MD_LOG_WARNING, "[SSBO] SDL_CreateGPUBuffer failed: %s", SDL_GetError());

        SDL_GPUTransferBufferCreateInfo ti = {};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size  = (Uint32)capacity_bytes;
        sdl_transfer_ = SDL_CreateGPUTransferBuffer(dev, &ti);
        if (!sdl_transfer_)
            MD_LOG(MD_LOG_WARNING, "[SSBO] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    glGenBuffers(1, &id);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, capacity_bytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    if (!id) fprintf(stderr, "[SSBO] creation failed (%d bytes)\n", capacity_bytes);
#endif
}

void SSBO::Upload(const void* data, int size_bytes, int offset) {
#ifdef MD_SDL_GPU
    if (sdl_buf_ && sdl_transfer_ && data && size_bytes > 0) {
        SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
        void* map = SDL_MapGPUTransferBuffer(dev, sdl_transfer_, SDL_TRUE /*cycle*/);
        if (map) {
            memcpy((uint8_t*)map + offset, data, (size_t)size_bytes);
            SDL_UnmapGPUTransferBuffer(dev, sdl_transfer_);
        }
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src = {};
        src.transfer_buffer = sdl_transfer_;
        src.offset          = (Uint32)offset;
        SDL_GPUBufferRegion dst_r = {};
        dst_r.buffer = sdl_buf_;
        dst_r.offset = (Uint32)offset;
        dst_r.size   = (Uint32)size_bytes;
        SDL_UploadToGPUBuffer(pass, &src, &dst_r, SDL_TRUE /*cycle*/);
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
    }
#endif
#ifdef MD_OPENGL43_ENABLED
    if (!id) return;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size_bytes, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
#endif
}

void SSBO::Bind(int binding_point) {
#ifdef MD_OPENGL43_ENABLED
    if (id) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)binding_point, id);
#else
    (void)binding_point;
#endif
}

void SSBO::Shutdown() {
#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (sdl_transfer_) { SDL_ReleaseGPUTransferBuffer(dev, sdl_transfer_); sdl_transfer_ = nullptr; }
    if (sdl_buf_)      { SDL_ReleaseGPUBuffer        (dev, sdl_buf_);      sdl_buf_      = nullptr; }
    sdl_cap_ = 0;
#endif
#ifdef MD_OPENGL43_ENABLED
    if (id) { glDeleteBuffers(1, &id); id = 0; }
#endif
}

#ifdef MD_SDL_GPU
void SSBO::UploadInCmd(SDL_GPUCommandBuffer* cmd, const void* data, int size_bytes, int offset) {
    if (!cmd || !sdl_buf_ || !sdl_transfer_ || !data || size_bytes <= 0) return;
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    void* map = SDL_MapGPUTransferBuffer(dev, sdl_transfer_, SDL_TRUE /*cycle*/);
    if (map) {
        memcpy((uint8_t*)map + offset, data, (size_t)size_bytes);
        SDL_UnmapGPUTransferBuffer(dev, sdl_transfer_);
    }
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = sdl_transfer_;
    src.offset          = (Uint32)offset;
    SDL_GPUBufferRegion dst_r = {};
    dst_r.buffer = sdl_buf_;
    dst_r.offset = (Uint32)offset;
    dst_r.size   = (Uint32)size_bytes;
    SDL_UploadToGPUBuffer(pass, &src, &dst_r, SDL_TRUE /*cycle*/);
    SDL_EndGPUCopyPass(pass);
}
#endif
