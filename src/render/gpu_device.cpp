#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/render/gpu_frame_timeline.h>
#include <monkey_dust/platform/md_log.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_timer.h>  // SDL_GetPerformanceCounter/Frequency -- sync-timing path

namespace md {

GpuDevice& GpuDevice::Get() {
    static GpuDevice instance;
    return instance;
}

bool GpuDevice::Init(SDL_Window* window) {
    // Vulkan validation layers add real per-pipeline compile overhead (confirmed:
    // ~4.5s stall creating the terrain_forward pipeline alone, reproducible every
    // launch) — gated on MD_GPU_VALIDATION, NOT plain DEBUG, since DEBUG is also
    // defined whenever MONKEY_DUST_EDITOR=ON (engine/CMakeLists.txt) regardless of
    // CMAKE_BUILD_TYPE, which would otherwise leave validation on for every normal
    // editor-enabled dev build, not just genuine CMAKE_BUILD_TYPE=Debug sessions.
    // Previously hardcoded `true` unconditionally ("TEMP: validation always on to
    // diagnose GPU crash") and never reverted — restore the real opt-in gate.
#ifdef MD_GPU_VALIDATION
    constexpr bool kDebug = true;
#else
    constexpr bool kDebug = false;
#endif
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, kDebug, NULL);
    if (!device_) {
        MD_LOG(MD_LOG_WARNING, "[GpuDevice] SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window)) {
        MD_LOG(MD_LOG_WARNING, "[GpuDevice] SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
        return false;
    }
    window_ = window;
    MD_LOG(MD_LOG_INFO, "[GpuDevice] Ready. Driver: %s", SDL_GetGPUDeviceDriver(device_));
    return true;
}

void GpuDevice::Shutdown() {
    if (device_) {
        // 2026-07-25: async uploads (e.g. GpuTexture::InitFromDDSArray)
        // submit a command buffer and release their host-side transfer
        // buffer immediately after, with no fence wait — correct as long
        // as SOME later frame's own sync (BeginFrame's fence wait, or just
        // elapsed wall-clock during normal play) catches up before the
        // buffer's memory is reused. A script that calls md.quit() only a
        // few ticks after a large upload (e.g. the 125-layer/699MB ground
        // texture array) skips all of that — SDL_DestroyGPUDevice below
        // could then run while the GPU copy is still in flight, corrupting
        // the Intel ANV driver's internal state (observed: SIGABRT heap
        // corruption and SIGSEGV in __munmap, both inside libvulkan_intel.so
        // during this exact Shutdown() call, both only under --exec
        // scenarios that quit quickly — never during normal interactive
        // use, which naturally gives every upload time to finish).
        SDL_WaitForGPUIdle(device_);
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
        window_ = nullptr;
    }
}

const char* GpuDevice::DriverName() const {
    return device_ ? SDL_GetGPUDeviceDriver(device_) : "none";
}

SDL_GPUCommandBuffer* GpuDevice::AcquireCommandBuffer() {
    return SDL_AcquireGPUCommandBuffer(device_);
}

SDL_GPUTexture* GpuDevice::AcquireSwapchainTexture(SDL_GPUCommandBuffer* cmd,
                                                    uint32_t* out_w, uint32_t* out_h) {
    SDL_GPUTexture* tex = nullptr;
    SDL_AcquireGPUSwapchainTexture(cmd, window_, &tex, out_w, out_h);
    return tex;
}

void GpuDevice::AdvanceFrameSlot() {
    frame_slot_ = (frame_slot_ + 1) % 3;
}

void GpuDevice::BeginFrame() {
    if (!prev_fence_ || !device_) return;
    SDL_WaitForGPUFences(device_, true, &prev_fence_, 1);
    SDL_ReleaseGPUFence(device_, prev_fence_);
    prev_fence_ = nullptr;
    // Timeline: fence signaled = GPU finished previous frame.
    GpuFrameTimeline::Get().OnFenceSignaled();
}

void GpuDevice::Submit(SDL_GPUCommandBuffer* cmd) {
    if (prev_fence_) { SDL_ReleaseGPUFence(device_, prev_fence_); prev_fence_ = nullptr; }

    if (sync_timing_) {
        // terrain-perf-measure (2026-08-12): serialize on THIS frame's fence
        // instead of deferring to next frame's BeginFrame() -- see header
        // doc comment for why the normal async path is blind to real GPU
        // cost under the TARGET_FPS software cap.
        SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        uint64_t t0 = SDL_GetPerformanceCounter();
        if (f) SDL_WaitForGPUFences(device_, true, &f, 1);
        uint64_t t1 = SDL_GetPerformanceCounter();
        uint64_t freq = SDL_GetPerformanceFrequency();
        last_gpu_ms_ = freq ? (float)((double)(t1 - t0) * 1000.0 / (double)freq) : 0.f;
        if (f) SDL_ReleaseGPUFence(device_, f);
        GpuFrameTimeline::Get().OnSubmit();
        return;
    }

    prev_fence_ = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    // Timeline: record submit timestamp for latency measurement.
    GpuFrameTimeline::Get().OnSubmit();
}

} // namespace md
#endif // MD_SDL_GPU
