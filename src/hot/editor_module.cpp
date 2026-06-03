#include <monkey_dust/hot/editor_module.h>
#include <monkey_dust/platform/md_log.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef __linux__
#include <dlfcn.h>
#endif

// C ABI function types from libeditor_panels.so
using EditorUploadHmapFn = void(*)(const float*, int, int, float, int, int);
using EditorInitFn     = void(*)(void* imgui_ctx, void* gpu, void* window,
                                  float overlay_top, const char* layout_path);
using EditorShutdownFn = void(*)(const char* layout_path);
using EditorBuildUIFn  = uint32_t(*)(float dt, float toolbar_h,
                                      char* status_msg, float* status_timer);
using EditorRenderFn   = void(*)(void* cmd, float dt, uint32_t active_flags);
using EditorDumpFn     = void(*)(void* file_ptr);
using EditorReloadShadersFn = void(*)();

// ── helpers ───────────────────────────────────────────────────────────────────

static int64_t s_mtime(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_mtim.tv_sec;
}

// ── singleton ─────────────────────────────────────────────────────────────────

EditorModule& EditorModule::Get() {
    static EditorModule inst;
    return inst;
}

// ── public ────────────────────────────────────────────────────────────────────

void EditorModule::Init(const char* so_path, const Config& cfg) {
    strncpy(so_path_, so_path, sizeof(so_path_) - 1);
    cfg_     = cfg;
    enabled_ = true;
    Load();
}

void EditorModule::Tick() {
#ifdef __linux__
    if (!enabled_) return;
    int64_t mtime = s_mtime(so_path_);
    if (mtime == -1) return;
    if (mtime == last_mtime_) return;
    MD_LOG(MD_LOG_INFO, "[EditorModule] change detected, reloading...");
    Load();
#endif
}

void EditorModule::Reload() {
    MD_LOG(MD_LOG_INFO, "[EditorModule] manual reload (F5)");
    Load();
}

uint32_t EditorModule::BuildUI(float dt, float toolbar_h,
                                char* status_msg, float* status_timer) {
#ifdef __linux__
    if (!handle_) return 0;
    auto* fn = reinterpret_cast<EditorBuildUIFn>(
        dlsym(handle_, "editor_panels_build_ui"));
    if (fn) return fn(dt, toolbar_h, status_msg, status_timer);
#endif
    (void)dt; (void)toolbar_h; (void)status_msg; (void)status_timer;
    return 0;
}

void EditorModule::Render(SDL_GPUCommandBuffer* cmd, float dt, uint32_t active_flags) {
#ifdef __linux__
    if (!handle_) return;
    auto* fn = reinterpret_cast<EditorRenderFn>(
        dlsym(handle_, "editor_panels_render"));
    if (fn) fn(cmd, dt, active_flags);
#endif
    (void)cmd; (void)dt; (void)active_flags;
}

void EditorModule::DumpState(void* file_ptr) {
#ifdef __linux__
    if (!handle_) return;
    auto* fn = reinterpret_cast<EditorDumpFn>(
        dlsym(handle_, "editor_panels_dump_state"));
    if (fn) fn(file_ptr);
#endif
    (void)file_ptr;
}

void EditorModule::UploadTerrainHeightmap(const float* hmap, int W, int H,
                                           float world_size_m, int cx, int cz) {
#ifdef __linux__
    if (!handle_) return;
    auto* fn = reinterpret_cast<EditorUploadHmapFn>(
        dlsym(handle_, "editor_panels_upload_hmap"));
    if (fn) fn(hmap, W, H, world_size_m, cx, cz);
#endif
    (void)hmap; (void)W; (void)H; (void)world_size_m; (void)cx; (void)cz;
}

void EditorModule::ReloadShaders() {
#ifdef __linux__
    if (!handle_) return;
    auto* fn = reinterpret_cast<EditorReloadShadersFn>(
        dlsym(handle_, "editor_panels_reload_shaders"));
    if (fn) fn();
#endif
}

void EditorModule::Shutdown() {
    Unload();
    enabled_ = false;
}

// ── load / unload ─────────────────────────────────────────────────────────────

void EditorModule::Load() {
#ifdef __linux__
    Unload();

    char tmp[280];
    snprintf(tmp, sizeof(tmp), "%s.tmp", so_path_);
    char cmd_buf[600];
    snprintf(cmd_buf, sizeof(cmd_buf), "cp -- '%s' '%s'", so_path_, tmp);
    if (system(cmd_buf) != 0) {
        MD_LOG(MD_LOG_WARNING, "[EditorModule] copy failed: %s", so_path_);
        return;
    }

    handle_ = dlopen(tmp, RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        MD_LOG(MD_LOG_WARNING, "[EditorModule] dlopen '%s': %s", tmp, dlerror());
        return;
    }

    auto* init_fn = reinterpret_cast<EditorInitFn>(
        dlsym(handle_, "editor_panels_init"));
    if (!init_fn) {
        MD_LOG(MD_LOG_WARNING, "[EditorModule] editor_panels_init not found: %s",
               dlerror());
        dlclose(handle_);
        handle_ = nullptr;
        return;
    }

    init_fn(cfg_.imgui_ctx, cfg_.gpu, cfg_.window,
            cfg_.overlay_top, cfg_.layout_path);

    last_mtime_ = s_mtime(so_path_);
    MD_LOG(MD_LOG_INFO, "[EditorModule] loaded: %s", so_path_);
#endif
}

void EditorModule::Unload() {
#ifdef __linux__
    if (!handle_) return;

    auto* shutdown_fn = reinterpret_cast<EditorShutdownFn>(
        dlsym(handle_, "editor_panels_shutdown"));
    if (shutdown_fn && cfg_.layout_path)
        shutdown_fn(cfg_.layout_path);

    dlclose(handle_);
    handle_ = nullptr;
    MD_LOG(MD_LOG_INFO, "[EditorModule] unloaded");
#endif
}
