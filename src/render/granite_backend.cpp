#include <monkey_dust/render/granite_backend.h>

#ifdef MD_USE_GRANITE

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "wsi.hpp"
#include "context.hpp"
#include "device.hpp"
#include "command_buffer.hpp"

#include <monkey_dust/platform/md_log.h>

#include <vector>

namespace md {
namespace {

// Same MdSdlWsiPlatform recipe as probes/granite_m2_wsi_probe.cpp and
// probes/granite_m3_imgui_spike.cpp (both live-verified on this HD 520) --
// wraps the caller-supplied SDL_Window, does not create its own.
class MdSdlWsiPlatform : public Vulkan::WSIPlatform {
public:
    explicit MdSdlWsiPlatform(SDL_Window* window) : window_(window) {}

    VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface)) {
            MD_LOG(MD_LOG_WARNING, "[GraniteBackend] SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
            return VK_NULL_HANDLE;
        }
        return surface;
    }

    std::vector<const char*> get_instance_extensions() override {
        Uint32 count = 0;
        char const* const* exts = SDL_Vulkan_GetInstanceExtensions(&count);
        return std::vector<const char*>(exts, exts + count);
    }

    uint32_t get_surface_width() override {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        return (uint32_t)w;
    }

    uint32_t get_surface_height() override {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        return (uint32_t)h;
    }

    bool alive(Vulkan::WSI&) override { return true; }
    void poll_input() override {}
    void poll_input_async(Granite::InputTrackerHandler*) override {}

private:
    SDL_Window* window_;
};

struct GraniteState {
    MdSdlWsiPlatform* platform = nullptr;
    Vulkan::WSI* wsi = nullptr;
    bool ready = false;
};

GraniteState& State() {
    static GraniteState s;
    return s;
}

} // namespace

GraniteBackend& GraniteBackend::Get() {
    static GraniteBackend inst;
    return inst;
}

bool GraniteBackend::IsBuilt() const { return true; }

bool GraniteBackend::Init(SDL_Window* window) {
    GraniteState& s = State();
    if (s.ready) return true;
    if (!window) {
        MD_LOG(MD_LOG_WARNING, "[GraniteBackend] Init() called with null window");
        return false;
    }

    // Same R1/M2-Крок-3 pitfall confirmed live twice already (probes'
    // documented history): Vulkan::WSI does not call Context::init_loader()
    // itself -- without this, volkGetInstanceProcAddr resolves to 0.
    if (!Vulkan::Context::init_loader((PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr())) {
        MD_LOG(MD_LOG_WARNING, "[GraniteBackend] Context::init_loader failed");
        return false;
    }

    s.platform = new MdSdlWsiPlatform(window);
    s.wsi = new Vulkan::WSI();
    s.wsi->set_platform(s.platform);

    Vulkan::Context::SystemHandles handles = {};
    if (!s.wsi->init_simple(1, handles)) {
        MD_LOG(MD_LOG_WARNING, "[GraniteBackend] wsi.init_simple failed");
        delete s.wsi;
        delete s.platform;
        s.wsi = nullptr;
        s.platform = nullptr;
        return false;
    }

    s.ready = true;
    return true;
}

void GraniteBackend::Shutdown() {
    GraniteState& s = State();
    if (!s.ready) return;
    // wsi (holds swapchain/surface, X11/xcb-backed) must be destroyed before
    // the caller destroys its SDL_Window -- confirmed live in M2 Крок 3
    // (reverse order segfaults deep in libvulkan_intel.so's
    // xcb_sync_destroy_fence). Caller owns the window and must not destroy
    // it before calling Shutdown().
    delete s.wsi;
    delete s.platform;
    s.wsi = nullptr;
    s.platform = nullptr;
    s.ready = false;
}

bool GraniteBackend::IsReady() const { return State().ready; }

void GraniteBackend::RenderEmptyFrame() {
    GraniteState& s = State();
    if (!s.ready) return;

    if (!s.wsi->begin_frame()) return;

    auto cmd = s.wsi->get_device().request_command_buffer();
    auto rp = s.wsi->get_device().get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
    rp.clear_color[0].float32[0] = 0.0f;
    rp.clear_color[0].float32[1] = 0.45f;
    rp.clear_color[0].float32[2] = 0.65f;
    rp.clear_color[0].float32[3] = 1.0f;
    cmd->begin_render_pass(rp);
    cmd->end_render_pass();
    s.wsi->get_device().submit(cmd);
    s.wsi->end_frame();
}

} // namespace md

#else // !MD_USE_GRANITE -- empty TU, zero Granite dependency

namespace md {
GraniteBackend& GraniteBackend::Get() { static GraniteBackend inst; return inst; }
bool GraniteBackend::IsBuilt() const { return false; }
bool GraniteBackend::Init(SDL_Window*) { return false; }
void GraniteBackend::Shutdown() {}
bool GraniteBackend::IsReady() const { return false; }
void GraniteBackend::RenderEmptyFrame() {}
} // namespace md

#endif // MD_USE_GRANITE
