#pragma once
#include <cstdint>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

// Portable flag for indirect-draw usage (pass as extra_sdl_usage to Init).
// Resolves to SDL_GPU_BUFFERUSAGE_INDIRECT in SDL_GPU builds, 0 otherwise.
#ifdef MD_SDL_GPU
static constexpr uint32_t SSBO_INDIRECT = SDL_GPU_BUFFERUSAGE_INDIRECT;
#else
static constexpr uint32_t SSBO_INDIRECT = 0u;
#endif

// GPU Shader Storage Buffer Object wrapper.
// OpenGL: glGenBuffers + GL_SHADER_STORAGE_BUFFER.
// SDL_GPU: SDL_GPUBuffer (COMPUTE_STORAGE_READ|WRITE) + SDL_GPUTransferBuffer for CPU uploads.
//
// extra_sdl_usage: additional SDL_GPU_BUFFERUSAGE_* flags OR'd onto the base usage.
// Use SSBO_INDIRECT for indirect draw command buffers.
class SSBO {
public:
    unsigned int id = 0; // OpenGL buffer id (0 in SDL_GPU-only builds)

    void Init(int capacity_bytes, uint32_t extra_sdl_usage = 0);
    // OpenGL: glBufferSubData. SDL_GPU: one-shot staging copy (own command buffer).
    void Upload(const void* data, int size_bytes, int offset = 0);
    // OpenGL: glBindBufferBase. SDL_GPU: no-op — binding done via StorageBindings.
    void Bind(int binding_point);
    void Shutdown();

#ifdef MD_SDL_GPU
    SDL_GPUBuffer* SDLBuffer() const { return sdl_buf_; }
    // Upload within an existing frame command buffer's copy pass.
    // Preferred over Upload() when a frame cmd is available (avoids extra submissions).
    void UploadInCmd(SDL_GPUCommandBuffer* cmd, const void* data, int size_bytes, int offset = 0);
#endif

private:
#ifdef MD_SDL_GPU
    SDL_GPUBuffer*         sdl_buf_      = nullptr;
    SDL_GPUTransferBuffer* sdl_transfer_ = nullptr;
    uint32_t               sdl_cap_      = 0;
#endif
};
