#pragma once
// Granite migration M2 (docs/GRANITE_MIGRATION_PLAN_M0_M6.md): optional
// Granite Vulkan device, dual-run alongside GpuDevice's SDL_GPU path.
// Empty no-op API when MD_USE_GRANITE is undefined (USE_GRANITE=OFF) --
// engine/ compiles and links with zero Granite dependency in that
// configuration (same Фаза A constraint as libgodot_bridge.h/.cpp).
struct SDL_Window;

namespace md {

class GraniteBackend {
public:
    static GraniteBackend& Get();

    // True only when compiled with MD_USE_GRANITE (USE_GRANITE=ON at
    // configure time). Init()/RenderEmptyFrame() are no-ops otherwise.
    bool IsBuilt() const;

    // Wraps the SAME SDL_Window the SDL_GPU path already owns (platform/
    // window.h's _wnd::ptr()) -- one window, one event pump, per M3's
    // "два бекенди не можуть ділити одне вікно" constraint resolved by
    // NOT creating a second window, only a second GPU device on it.
    bool Init(SDL_Window* window);
    void Shutdown();
    bool IsReady() const;

    // M2's own exit criterion: an empty frame clears in game/monkey_dust
    // under USE_GRANITE=ON. Teal, matching probes/granite_m2_wsi's and
    // probes/granite_m3_imgui_spike's visual proof color for continuity.
    void RenderEmptyFrame();
};

} // namespace md
