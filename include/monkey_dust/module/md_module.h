#pragma once
#include <cstring>

// ── MdModule ──────────────────────────────────────────────────────────────────
// O3DE Gem-inspired lightweight module registry for plug-in architecture.
// Intended for tools/ targets (editor panels, converters, demo plugins) that
// need a standardized load/unload lifecycle without AzCore dependency.
//
// Usage (tools/editor or tools/flare_demo):
//   MdModule mod{};
//   strncpy(mod.name,    "FlareMapInspector", sizeof(mod.name) - 1);
//   strncpy(mod.version, "1.0.0",             sizeof(mod.version) - 1);
//   mod.on_load   = [](void* ctx) -> bool { /* register panel */ return true; };
//   mod.on_unload = [](void* ctx) { /* cleanup */ };
//   MdModuleRegistry::Get().Register(mod);
//   MdModuleRegistry::Get().Load("FlareMapInspector", &editor_state);
//
// ctx is caller-defined (EditorState*, GameState*, etc.) — registry is generic.
// on_load returns false to signal init failure; Load() returns the result.
//
// JSON metadata (optional, for tooling documentation):
//   data/modules/<name>.module.json  {"name":"...","version":"...","description":"..."}
//   Loaded by tools at startup for UI display; not required for runtime.

static constexpr int MD_MODULE_NAME_LEN    = 64;
static constexpr int MD_MODULE_VERSION_LEN = 16;

using MdModuleOnLoad   = bool(*)(void* ctx);  // false = initialization failed
using MdModuleOnUnload = void(*)(void* ctx);

struct MdModule {
    char           name   [MD_MODULE_NAME_LEN]    = {};
    char           version[MD_MODULE_VERSION_LEN] = {};
    MdModuleOnLoad   on_load   = nullptr;
    MdModuleOnUnload on_unload = nullptr;
};

// ── MdModuleRegistry ─────────────────────────────────────────────────────────
// Singleton flat-array registry for named modules.
// Thread-safety: not thread-safe — register/load from main thread only.
class MdModuleRegistry {
public:
    static constexpr int MAX_MODULES = 16;

    static MdModuleRegistry& Get() {
        static MdModuleRegistry inst;
        return inst;
    }

    // Register a module descriptor. Duplicate names update the existing entry.
    // Returns false if name is empty or registry is full.
    bool Register(const MdModule& m) noexcept {
        if (!m.name[0]) return false;
        for (int i = 0; i < count_; ++i) {
            if (strcmp(modules_[i].name, m.name) == 0) {
                modules_[i] = m;
                return true;
            }
        }
        if (count_ >= MAX_MODULES) return false;
        modules_[count_] = m;
        loaded_ [count_] = false;
        ++count_;
        return true;
    }

    // Call on_load for the named module. No-op if already loaded or not found.
    // Returns false on load failure or if module not found.
    bool Load(const char* name, void* ctx) noexcept {
        int idx = find_idx(name);
        if (idx < 0 || loaded_[idx]) return idx >= 0;  // not found → false; already loaded → true
        if (modules_[idx].on_load && !modules_[idx].on_load(ctx)) return false;
        loaded_[idx] = true;
        return true;
    }

    // Call on_unload for the named module. No-op if not loaded or not found.
    void Unload(const char* name, void* ctx) noexcept {
        int idx = find_idx(name);
        if (idx < 0 || !loaded_[idx]) return;
        if (modules_[idx].on_unload) modules_[idx].on_unload(ctx);
        loaded_[idx] = false;
    }

    // Unload all currently-loaded modules in reverse registration order.
    void UnloadAll(void* ctx) noexcept {
        for (int i = count_ - 1; i >= 0; --i) {
            if (!loaded_[i]) continue;
            if (modules_[i].on_unload) modules_[i].on_unload(ctx);
            loaded_[i] = false;
        }
    }

    // Find by name; returns nullptr if not found.
    const MdModule* Find(const char* name) const noexcept {
        int idx = find_idx(name);
        return idx >= 0 ? &modules_[idx] : nullptr;
    }

    bool IsLoaded(const char* name) const noexcept {
        int idx = find_idx(name);
        return idx >= 0 && loaded_[idx];
    }

    // Clear registry WITHOUT calling on_unload. Call UnloadAll first if needed.
    void Clear() noexcept { count_ = 0; memset(loaded_, 0, sizeof(loaded_)); }

    int  Count()  const noexcept { return count_; }

private:
    MdModuleRegistry() = default;

    int find_idx(const char* name) const noexcept {
        if (!name || !name[0]) return -1;
        for (int i = 0; i < count_; ++i)
            if (strcmp(modules_[i].name, name) == 0) return i;
        return -1;
    }

    MdModule modules_[MAX_MODULES] = {};
    bool     loaded_ [MAX_MODULES] = {};
    int      count_ = 0;
};
