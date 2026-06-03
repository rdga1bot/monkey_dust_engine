#include <monkey_dust/hot/gameplay_module.h>
#include <monkey_dust/platform/md_log.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef __linux__
#include <dlfcn.h>
#endif

using GameplayInitFn     = void(*)();
using GameplayShutdownFn = void(*)();

// ── helpers ───────────────────────────────────────────────────────────────────

static int64_t s_mtime(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_mtim.tv_sec;
}

// ── singleton ─────────────────────────────────────────────────────────────────

GameplayModule& GameplayModule::Get() {
    static GameplayModule inst;
    return inst;
}

// ── public ────────────────────────────────────────────────────────────────────

void GameplayModule::Init(const char* so_path) {
    strncpy(so_path_, so_path, sizeof(so_path_) - 1);
    enabled_ = true;
    Load();
}

void GameplayModule::Tick() {
#ifdef __linux__
    if (!enabled_) return;
    int64_t mtime = s_mtime(so_path_);
    if (mtime == -1) return;           // .so not built yet — no-op
    if (mtime == last_mtime_) return;  // unchanged
    MD_LOG(MD_LOG_INFO, "[GameplayModule] change detected, reloading...");
    Load();
#endif
}

void GameplayModule::Shutdown() {
    Unload();
    enabled_ = false;
}

// ── load / unload ─────────────────────────────────────────────────────────────

void GameplayModule::Load() {
#ifdef __linux__
    Unload();

    // Copy .so → .tmp so ninja can overwrite the original while we hold .tmp open.
    char tmp[280];
    snprintf(tmp, sizeof(tmp), "%s.tmp", so_path_);
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "cp -- '%s' '%s'", so_path_, tmp);
    if (system(cmd) != 0) {
        MD_LOG(MD_LOG_WARNING, "[GameplayModule] copy failed: %s → %s", so_path_, tmp);
        return;
    }

    // RTLD_LOCAL: .so symbols don't pollute global namespace.
    // RTLD_NOW:   resolve all symbols immediately → catch missing deps early.
    handle_ = dlopen(tmp, RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        MD_LOG(MD_LOG_WARNING, "[GameplayModule] dlopen '%s': %s", tmp, dlerror());
        return;
    }

    auto* init_fn = reinterpret_cast<GameplayInitFn>(dlsym(handle_, "gameplay_init"));
    if (!init_fn) {
        MD_LOG(MD_LOG_WARNING, "[GameplayModule] gameplay_init not found: %s", dlerror());
        dlclose(handle_);
        handle_ = nullptr;
        return;
    }

    init_fn();
    last_mtime_ = s_mtime(so_path_);
    MD_LOG(MD_LOG_INFO, "[GameplayModule] loaded: %s", so_path_);
#endif
}

void GameplayModule::Unload() {
#ifdef __linux__
    if (!handle_) return;

    auto* shutdown_fn = reinterpret_cast<GameplayShutdownFn>(
        dlsym(handle_, "gameplay_shutdown"));
    if (shutdown_fn) shutdown_fn();

    dlclose(handle_);
    handle_ = nullptr;
    MD_LOG(MD_LOG_INFO, "[GameplayModule] unloaded");
#endif
}
