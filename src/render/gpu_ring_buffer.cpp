#include <monkey_dust/render/gpu_ring_buffer.h>

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#include <monkey_dust/platform/md_log.h>
#include <cstdio>

// Reinterpret the stored void* as GLsync to avoid pulling <GL/gl.h> into the header.
static inline GLsync& as_sync(void*& p) { return reinterpret_cast<GLsync&>(p); }

void GpuRingBuffer::Init(uint32_t size_bytes, int binding_hint) {
    size_     = size_bytes;
    binding_  = binding_hint;
    cur_      = 0;
    base_ptr_ = nullptr;

    if (size_ == 0) {
        MD_LOG(MD_LOG_WARNING, "[GpuRingBuffer] Init: size_bytes == 0");
        return;
    }

    glGenBuffers(1, &gl_buf_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_buf_);

    // Allocate N_FRAMES × size bytes with persistent coherent write access.
    // GL_MAP_COHERENT_BIT: CPU writes visible to GPU without explicit barrier.
    // GL_DYNAMIC_STORAGE_BIT: allows glBufferSubData fallback if needed.
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

    if (!base_ptr_) {
        MD_LOG(MD_LOG_WARNING, "[GpuRingBuffer] glMapBufferRange failed for %u bytes",
               size_ * N_FRAMES);
    }
}

void GpuRingBuffer::Shutdown() {
    if (!gl_buf_) return;
    for (int i = 0; i < N_FRAMES; ++i) {
        if (as_sync(fences_[i])) {
            glDeleteSync(as_sync(fences_[i]));
            fences_[i] = nullptr;
        }
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl_buf_);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDeleteBuffers(1, &gl_buf_);
    gl_buf_   = 0;
    base_ptr_ = nullptr;
}

void GpuRingBuffer::WaitFence(int slot) {
    GLsync& fence = as_sync(fences_[slot]);
    if (!fence) return;
    // Wait up to 10ms; in practice always instant with 3-frame ring at 60fps.
    GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 10'000'000);
    if (result == GL_TIMEOUT_EXPIRED || result == GL_WAIT_FAILED) {
        MD_LOG(MD_LOG_WARNING, "[GpuRingBuffer] fence wait timeout on slot %d", slot);
    }
    glDeleteSync(fence);
    fence = nullptr;
}

void* GpuRingBuffer::MapWrite() {
    if (!base_ptr_) return nullptr;
    WaitFence(cur_);
    return static_cast<uint8_t*>(base_ptr_) + (size_t)cur_ * size_;
}

void GpuRingBuffer::Unmap() {
    // Coherent mapping: writes already visible to GPU — no explicit flush needed.
    // Kept for SDL_GPU API symmetry (SDL_UnmapGPUTransferBuffer equivalent).
}

void GpuRingBuffer::BindStorage(int slot) {
    if (!gl_buf_) return;
    // Bind the current frame's sub-range as an SSBO.
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,
                      (GLuint)slot, gl_buf_,
                      (GLintptr)cur_ * size_, size_);
}

void GpuRingBuffer::Advance() {
    if (!gl_buf_) return;
    // Insert fence: signals when GPU finishes reading this slot.
    GLsync& fence = as_sync(fences_[cur_]);
    if (fence) glDeleteSync(fence);
    fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    // Rotate to next slot.
    cur_ = (cur_ + 1) % N_FRAMES;
}

#endif
