#pragma once
#ifdef MD_SDL_GPU
// ─────────────────────────────────────────────────────────────────────────────
// GpuDevice — SDL_GPU device singleton. Step 0 of SDL_GPU migration.
//
// Manages the SDL_GPUDevice* lifetime and per-frame command buffer flow.
// Call Init() once after window_init(). Falls back gracefully if Vulkan is
// unavailable: IsReady() returns false and the game continues on OpenGL.
//
// Per-frame sequence (Step 9+):
//   auto* cmd  = GpuDevice::Get().AcquireCommandBuffer();
//   auto* swap = GpuDevice::Get().AcquireSwapchainTexture(cmd, &w, &h);
//   // ... record passes ...
//   GpuDevice::Get().Submit(cmd);
// ─────────────────────────────────────────────────────────────────────────────

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace md {

class GpuDevice {
public:
    static GpuDevice& Get();

    // Create SDL_GPUDevice + claim window. Call after window_init().
    // Returns false if no suitable GPU driver found.
    bool       Init(SDL_Window* window);
    void       Shutdown();

    bool           IsReady()    const { return device_ != nullptr; }
    SDL_GPUDevice* SDLDevice()  const { return device_; }
    SDL_Window*    Window()     const { return window_; }
    const char*    DriverName() const;

    // Per-frame: get a command buffer for recording this frame's GPU work.
    SDL_GPUCommandBuffer* AcquireCommandBuffer();

    // Acquire the swapchain texture to render into. Must be called inside a
    // command buffer acquired with AcquireCommandBuffer().
    SDL_GPUTexture* AcquireSwapchainTexture(SDL_GPUCommandBuffer* cmd,
                                             uint32_t* out_w, uint32_t* out_h);

    // Advance the frame slot (0→1→2→0). Call once per frame before any uploads.
    void AdvanceFrameSlot();
    // Current triple-buffer slot (0..2). Per-frame SSBOs/RingBuffers use this.
    int  FrameSlot() const { return frame_slot_; }

    // Wait for previous frame's fence, then release it.
    // Call once at the top of each frame BEFORE AdvanceFrameSlot.
    void BeginFrame();

    // Submit the command buffer and acquire a fence for next-frame sync.
    void Submit(SDL_GPUCommandBuffer* cmd);

    // terrain-perf-measure (2026-08-12): opt-in synchronous GPU timing.
    // Normal Submit() is deliberately async (fence released a whole frame
    // late, in BeginFrame() -- lets CPU and GPU overlap). That's exactly
    // why frame_dt()-based measurement is blind to real GPU cost here: at
    // TARGET_FPS=60 the GPU has slack, so the software frame-cap
    // (main.cpp) eats the difference and frame_dt() never moves. When
    // SetSyncTiming(true), Submit() instead waits on THIS frame's fence
    // immediately and times the wait -- LastGpuMs() is real serialized GPU
    // execution time (submit-to-fence-signal), at the cost of killing
    // CPU/GPU pipelining for as long as the flag is on. NOT a shippable
    // mode -- test-only, default off, md.set_gpu_sync_timing(bool).
    void  SetSyncTiming(bool on) { sync_timing_ = on; }
    bool  SyncTiming() const     { return sync_timing_; }
    float LastGpuMs() const      { return last_gpu_ms_; }

private:
    GpuDevice() = default;
    SDL_GPUDevice* device_       = nullptr;
    SDL_Window*    window_       = nullptr;
    int            frame_slot_   = 0;
    SDL_GPUFence*  prev_fence_   = nullptr;
    bool           sync_timing_  = false;
    float          last_gpu_ms_  = 0.f;
};

} // namespace md

#endif // MD_SDL_GPU
