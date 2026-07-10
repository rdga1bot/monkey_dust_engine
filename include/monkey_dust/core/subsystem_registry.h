#pragma once
#include <cstring>
#include <cstdio>

// SubsystemRegistry — per-tick enable/disable toggle for the hardcoded
// Tick*() sequence in game/src/logic_tick.cpp::RunLogicTick (ARCHITECTURE_IDEAS.md
// #5). Execution ORDER stays exactly as written — this only wraps each call
// with a name + on/off flag so a subsystem can be disabled from the console
// (e.g. for isolating which tick is causing a frame spike) without editing
// and recompiling logic_tick.cpp.
//
// Per-name TIMING already exists via TimingSystem (TIMING_BEGIN/TIMING_END,
// engine/include/monkey_dust/platform/timing_system.h) — this does NOT
// duplicate that. It only adds the enable/disable axis, which TimingSystem
// doesn't have.
//
// Fixed array (MAX_SUBSYSTEMS), linear find-by-name — registration only
// happens a handful of times at startup (one IsEnabled() call per Tick* per
// logic tick — 10/sec — is negligible next to what each Tick* itself does).
class SubsystemRegistry {
public:
    static SubsystemRegistry& Get() { static SubsystemRegistry inst; return inst; }

    static constexpr int MAX_SUBSYSTEMS = 16;
    static constexpr int NAME_LEN       = 32;

    // Registers `name` as enabled on first call; returns its current enabled state.
    bool IsEnabled(const char* name) {
        Entry* e = FindOrCreate(name);
        return e ? e->enabled : true;  // unregistered (budget hit) -> fail open, never silently skip work
    }

    void SetEnabled(const char* name, bool on) {
        Entry* e = FindOrCreate(name);
        if (e) e->enabled = on;
    }

    int Count() const { return count_; }
    const char* NameAt(int i) const { return (i >= 0 && i < count_) ? entries_[i].name : ""; }
    bool EnabledAt(int i)     const { return (i >= 0 && i < count_) ? entries_[i].enabled : false; }

private:
    SubsystemRegistry() = default;
    struct Entry { char name[NAME_LEN] = {}; bool enabled = true; };
    Entry entries_[MAX_SUBSYSTEMS] = {};
    int   count_ = 0;

    Entry* FindOrCreate(const char* name) {
        for (int i = 0; i < count_; ++i)
            if (strcmp(entries_[i].name, name) == 0) return &entries_[i];
        if (count_ >= MAX_SUBSYSTEMS) {
            fprintf(stderr, "[SubsystemRegistry] MAX_SUBSYSTEMS (%d) exceeded, cannot register %s\n",
                    MAX_SUBSYSTEMS, name);
            return nullptr;
        }
        Entry& e = entries_[count_++];
        snprintf(e.name, NAME_LEN, "%s", name);
        e.enabled = true;
        return &e;
    }
};
