#pragma once
#include <cstdint>

// ── EditorModule ──────────────────────────────────────────────────────────────
// Manages hot-reload of libeditor_panels.so at runtime via dlopen/dlclose.
// On mtime change (or F5): shutdown panels (saves layout to JSON), unloads old
// .so, loads new, calls editor_panels_init() — panels restored from JSON.
//
// Lifecycle:
//   EditorModule::Get().Init(so_path, imgui_ctx, gpu, wnd, overlay_top, layout);
//   // in game loop:
//   uint32_t flags = EditorModule::Get().BuildUI(dt, toolbar_h, msg, &timer);
//   EditorModule::Get().Render(cmd, dt, prev_flags);
//   // F5 hotkey:
//   EditorModule::Get().Reload();
//   // on exit:
//   EditorModule::Get().Shutdown();
//
// Active viewport flags (returned by BuildUI, consumed by Render):
//   bit 0: 3D World tab active
//   bit 1: Character Preview tab active
//   bit 2: Heightmap tab active
//   bit 3: Map View tab active
//
// Build: cmake -DMONKEY_DUST_EDITOR_HOT_RELOAD=ON + ninja monkey_dust_editor_panels
// Reload: edit any tools/editor/*.cpp → ninja monkey_dust_editor_panels (~5-10s)
//         → next F5 in editor picks up new panels without closing window.

struct SDL_GPUCommandBuffer;
struct SDL_GPUDevice;
struct SDL_Window;
struct ImGuiContext;

class EditorModule {
public:
    static EditorModule& Get();

    struct Config {
        ImGuiContext* imgui_ctx  = nullptr;
        SDL_GPUDevice* gpu       = nullptr;
        SDL_Window*    window    = nullptr;
        float          overlay_top = 0.f;
        const char*    layout_path = nullptr;
    };

    void Init(const char* so_path, const Config& cfg);

    // Poll mtime — triggers reload if .so changed on disk.
    void Tick();

    // Force reload (F5 hotkey).
    void Reload();

    // Called inside ImGui frame. Returns active-viewport bitmask.
    uint32_t BuildUI(float dt, float toolbar_h,
                     char* status_msg, float* status_timer);

    // Called before ImGui present — renders RTTs.
    void Render(SDL_GPUCommandBuffer* cmd, float dt, uint32_t active_flags);

    // Write editor state to file (F9 bug capture).
    void DumpState(void* file_ptr);

    // Bridge: terrain panel → World3D upload (called from main.cpp bridge function).
    void UploadTerrainHeightmap(const float* hmap, int W, int H,
                                 float world_size_m, int chunk_x, int chunk_z);

    // Reload all shader pipelines (/reload-shaders console command).
    void ReloadShaders();

    void Shutdown();

    bool IsLoaded() const { return handle_ != nullptr; }

private:
    EditorModule() = default;

    void Load();
    void Unload();

    char     so_path_[256]     = {};
    Config   cfg_              = {};
    void*    handle_           = nullptr;
    int64_t  last_mtime_       = -1;
    bool     enabled_          = false;
};
