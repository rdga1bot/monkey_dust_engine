#pragma once
#include <cstdint>

// ── GameplayModule ────────────────────────────────────────────────────────────
// Manages hot-reload of libgameplay.so at runtime via dlopen/dlclose.
// On mtime change: unloads old .so, copies to .tmp (allows ninja to
// overwrite the original while the game runs), loads new, calls gameplay_init().
//
// gameplay_init() in the .so calls RegisterAllBTBindings() which overwrites
// the BT action registry with new function pointers — the game loop picks up
// new AI behavior on the next logic tick with zero downtime.
//
// Lifecycle:
//   GameplayModule::Get().Init("build/hot/libgameplay.so");
//   // in game loop:
//   GameplayModule::Get().Tick();
//   // on exit:
//   GameplayModule::Get().Shutdown();
//
// Build libgameplay.so:
//   cmake -DMONKEY_DUST_HOT_RELOAD=ON ...
//   ninja monkey_dust_gameplay    # ~2-4s, no game restart needed
//
// Platform: Linux only (dlopen). No-op on other platforms.

class GameplayModule {
public:
    static GameplayModule& Get();

    // Load .so and start watching for changes.
    // so_path: e.g. "build/hot/libgameplay.so"
    // Falls back gracefully if .so not found.
    void Init(const char* so_path);

    // Call once per frame from game loop.
    // Triggers reload when .so mtime changes.
    void Tick();

    // Unload .so and stop watching.
    void Shutdown();

    bool IsLoaded() const { return handle_ != nullptr; }
    bool IsEnabled() const { return enabled_; }

private:
    GameplayModule() = default;

    void Load();
    void Unload();

    char     so_path_[256] = {};
    void*    handle_       = nullptr;
    int64_t  last_mtime_   = -1;
    bool     enabled_      = false;
};
