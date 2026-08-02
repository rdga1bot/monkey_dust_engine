#pragma once
// EditorCmdRegistry — host-owned command registry (EDITOR_AUTOMATION_PLAN_v1.md
// Phase 0 falsification probe). Storage lives here, in the host executable,
// NEVER inside libeditor_panels.so — the hot-reloadable panels module
// registers its commands on load and MUST unregister them on unload via
// UnregisterModule(), or dlclose leaves dangling function pointers behind
// (same invariant class as the Flecs structural-deferral rule).
//
// Header-only Meyer's singleton, same pattern as EditorPanelRegistry
// (editor_panel_registry.h) — proven precedent in this codebase for a
// registry shared correctly between the host .exe and the panels .so across
// a dlopen/dlclose cycle (inline function-local static, default ELF
// visibility de-duplicates the symbol across the two binaries).
//
// Phase 0 scope only: args-less CmdFn, no undo, no schema. Phase 1.1 extends
// this to CmdResult(*)(const CmdArgs&, MdRegistry&) with an undo fn and an
// arg schema — deliberately not designed yet, so Phase 0 stays the cheapest
// possible test of the one binary hypothesis: a host-owned registry survives
// a panels hot-reload cycle with no stale pointers.
#include <monkey_dust/ai/fnv.h>
#include <cstdint>

using CmdFn = void (*)();

class EditorCmdRegistry {
public:
    static constexpr int MAX_COMMANDS = 64;

    static EditorCmdRegistry& Get() {
        static EditorCmdRegistry inst;
        return inst;
    }

    // Registers a command owned by owner_module_id. Re-registration (same
    // name hash) updates execute/undo/owner in place. Silently drops if
    // MAX_COMMANDS is reached (no assert per project rules).
    bool Register(const char* name, CmdFn execute, CmdFn undo, uint32_t owner_module_id) {
        uint32_t h = md::fnv1a_rt(name);
        for (int i = 0; i < count_; ++i) {
            if (entries_[i].active && entries_[i].name_hash == h) {
                entries_[i].name = name;
                entries_[i].execute = execute;
                entries_[i].undo = undo;
                entries_[i].owner_module_id = owner_module_id;
                return true;
            }
        }
        if (count_ >= MAX_COMMANDS) return false;
        Entry& e = entries_[count_++];
        e.name_hash = h;
        e.name = name;
        e.execute = execute;
        e.undo = undo;
        e.owner_module_id = owner_module_id;
        e.active = true;
        return true;
    }

    // Purges every command owned by owner_module_id — call from the panels
    // module's shutdown entry point before dlclose. Compacts the array so
    // Count()/iteration never sees stale inactive holes.
    void UnregisterModule(uint32_t owner_module_id) {
        int w = 0;
        for (int r = 0; r < count_; ++r) {
            if (entries_[r].owner_module_id == owner_module_id) continue;
            if (w != r) entries_[w] = entries_[r];
            ++w;
        }
        count_ = w;
    }

    // Finds and calls the named command's execute fn. Returns false if not
    // found (name unregistered or never existed).
    bool Dispatch(const char* name) const {
        uint32_t h = md::fnv1a_rt(name);
        for (int i = 0; i < count_; ++i) {
            if (entries_[i].active && entries_[i].name_hash == h) {
                if (entries_[i].execute) entries_[i].execute();
                return true;
            }
        }
        return false;
    }

    int Count() const { return count_; }

    // Test-only: full reset (mirrors EditorPanelRegistry::Clear()).
    void Clear() { count_ = 0; }

private:
    EditorCmdRegistry() = default;

    struct Entry {
        uint32_t name_hash = 0;
        const char* name = nullptr;
        CmdFn execute = nullptr;
        CmdFn undo = nullptr;
        uint32_t owner_module_id = 0;
        bool active = false;
    };

    Entry entries_[MAX_COMMANDS];
    int count_ = 0;
};
