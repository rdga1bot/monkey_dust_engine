#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/render/gpu_frame_timeline.h>
#include <monkey_dust/platform/md_log.h>
#include <SDL3/SDL_gpu.h>

namespace md {

GpuDevice& GpuDevice::Get() {
    static GpuDevice instance;
    return instance;
}

bool GpuDevice::Init(SDL_Window* window) {
    constexpr bool kDebug = true; // TEMP: validation always on to diagnose GPU crash
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
    prev_fence_ = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    // Timeline: record submit timestamp for latency measurement.
    GpuFrameTimeline::Get().OnSubmit();
}

} // namespace md
#endif // MD_SDL_GPU
