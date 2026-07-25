#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/render/gpu_frame_timeline.h>
#include <monkey_dust/platform/md_log.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_properties.h>
// Raw Vulkan header — deliberate, narrowly-scoped exception to this
// project's "GPU HAL, not raw GL/Vulkan" rule (CLAUDE.md), confined to
// this ONE translation unit. Needed only to fill VkPhysicalDeviceShader
// QuadControlFeaturesKHR for SDL_GPUVulkanOptions.feature_list below —
// no other file in the engine includes this header. See task/plan notes
// 2026-07-25 (VK_KHR_shader_quad_control runner-up-blend skip) for why:
// live vulkaninfo/slangc checks on this exact Intel HD 520/Mesa ANV
// confirmed driver + compiler support; SDL_GPU has no higher-level API
// for requesting extra Vulkan device extensions/features, only this
// escape hatch (SDL_CreateGPUDeviceWithProperties +
// SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, SDL >= 3.4.0).
#include <vulkan/vulkan_core.h>

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

    // Opt into VK_KHR_shader_quad_control (task 2026-07-25: skip the
    // terrain runner-up-blend's texture-array samples on quads where no
    // pixel needs them, using subgroupQuadAny — see terrain_forward.slang).
    // Graceful fallback to the plain SDL_CreateGPUDevice() path if this
    // device/driver combo rejects the extension (unconfirmed on hardware
    // other than this session's own Intel HD 520/Mesa 26.1.4) — never
    // hard-fail startup over an optional perf extension.
    VkPhysicalDeviceShaderQuadControlFeaturesKHR quad_control_features = {};
    quad_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_QUAD_CONTROL_FEATURES_KHR;
    quad_control_features.pNext = nullptr;
    quad_control_features.shaderQuadControl = VK_TRUE;

    const char* extra_device_ext[] = {
        "VK_KHR_shader_quad_control",
        "VK_KHR_shader_maximal_reconvergence",  // quad_control's spec lists this as a dependency
    };

    SDL_GPUVulkanOptions vk_opts = {};
    vk_opts.vulkan_api_version    = VK_API_VERSION_1_3;
    vk_opts.feature_list          = &quad_control_features;
    vk_opts.device_extension_count = 2;
    vk_opts.device_extension_names = extra_device_ext;

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, kDebug);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &vk_opts);
    device_ = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);

    if (!device_) {
        MD_LOG(MD_LOG_WARNING,
               "[GpuDevice] SDL_CreateGPUDeviceWithProperties (quad_control) failed: %s — "
               "falling back to plain SDL_CreateGPUDevice", SDL_GetError());
        device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, kDebug, NULL);
    }
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
