#pragma once
#include <cstdint>

// GpuRingBuffer — N-frame cycling SSBO for per-frame CPU→GPU data uploads.
//
// OpenGL path (current, GL 4.4+):
//   One large buffer = size_bytes * N_FRAMES, persistently mapped (coherent).
//   Each frame writes to a different sub-range → no GPU-CPU stall.
//   Fence per slot: before writing frame N, wait for fence from frame N-N_FRAMES.
//
// SDL_GPU path (future migration):
//   staging_[N] = SDL_GPUTransferBuffer[N]  (CPU-mapped)
//   device_     = SDL_GPUBuffer              (GPU storage read)
//   Unmap()     → SDL_UploadToGPUBuffer(cmd, staging_[cur], device_, cycle=true)
//   Advance()   → rotate cur_ slot
//
// Usage (every frame):
//   void* ptr = ring.MapWrite();      // get writable region for this frame
//   memcpy(ptr, cpu_data, size);
//   ring.Unmap();                     // flush (no-op for coherent OpenGL path)
//   ring.BindStorage(slot);           // bind for shader access
//   // ... dispatch compute / draw ...
//   ring.Advance();                   // insert fence + rotate slot
//
// IMPORTANT: BindStorage() binds the CURRENT frame's sub-buffer.
//            Advance() must be called AFTER all draws that use this buffer.

#ifdef MD_OPENGL43_ENABLED

class GpuRingBuffer {
public:
    static constexpr int N_FRAMES = 3;

    // Allocate N_FRAMES sub-buffers of size_bytes each.
    // binding_hint stored for BindStorage() convenience (optional).
    void Init(uint32_t size_bytes, int binding_hint = -1);
    void Shutdown();

    // Returns a CPU-writable pointer to the current frame's data region.
    // Valid until Advance() is called.
    void* MapWrite();

    // For coherent persistent mapping this is a no-op — writes are already
    // visible to the GPU.  Kept for API symmetry with SDL_GPU staging path.
    void Unmap();

    // Bind current frame's sub-buffer as GL_SHADER_STORAGE_BUFFER at slot.
    void BindStorage(int slot);

    // Insert a GL fence for the current slot, then rotate to the next slot.
    // Call this AFTER all draw/compute calls that read from this buffer.
    void Advance();

    uint32_t size_bytes() const { return size_; }
    int      current()    const { return cur_; }

private:
    void WaitFence(int slot);

    unsigned int gl_buf_   = 0;         // single large GL buffer object
    void*        base_ptr_ = nullptr;   // persistently-mapped base address
    void*        fences_[N_FRAMES] = {}; // GLsync per slot (opaque void* for header)
    uint32_t     size_     = 0;
    int          cur_      = 0;
    int          binding_  = -1;
};

#else

// No-op stubs for non-GL43 builds.
class GpuRingBuffer {
public:
    void  Init(uint32_t, int = -1) {}
    void  Shutdown() {}
    void* MapWrite() { return nullptr; }
    void  Unmap()    {}
    void  BindStorage(int) {}
    void  Advance()  {}
    uint32_t size_bytes() const { return 0; }
    int      current()    const { return 0; }
};

#endif
