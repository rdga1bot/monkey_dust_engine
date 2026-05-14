#pragma once
#include <cstdint>
#include <monkey_dust/ai/fnv.h>
#include <monkey_dust/platform/md_log.h>

// NamedBranchRegistry — CATHODE DecoratorBranch pattern.
// Each named branch (e.g. "THREAT_AWARE_BRANCH") maps to a bool slot.
// BT DecoratorNamedBranch checks the registry on every tick;
// external systems (DirectorSystem, FlowGraph) activate/deactivate branches.
//
// MAX_BRANCHES=16 slots; name stored as FNV-1a hash.

static constexpr uint8_t MAX_NAMED_BRANCHES = 16;

class NamedBranchRegistry {
public:
    static NamedBranchRegistry& Get() noexcept {
        static NamedBranchRegistry inst;
        return inst;
    }

    // Activate or deactivate a named branch.
    void Set(uint32_t name_hash, bool active) noexcept {
        for (uint8_t i = 0; i < count_; ++i) {
            if (slots_[i].hash == name_hash) { slots_[i].active = active; return; }
        }
        if (active) {
            if (count_ >= MAX_NAMED_BRANCHES) {
                MD_LOG(MD_LOG_WARNING, "NamedBranchRegistry: full (%d)", MAX_NAMED_BRANCHES);
                return;
            }
            slots_[count_++] = { name_hash, true };
        }
    }

    void Set(const char* name, bool active) noexcept {
        Set(md::fnv1a_rt(name), active);
    }

    bool IsActive(uint32_t name_hash) const noexcept {
        for (uint8_t i = 0; i < count_; ++i)
            if (slots_[i].hash == name_hash) return slots_[i].active;
        return false;  // unknown branch = inactive
    }

    bool IsActive(const char* name) const noexcept {
        return IsActive(md::fnv1a_rt(name));
    }

    void Clear() noexcept { count_ = 0; }
    uint8_t count() const noexcept { return count_; }

private:
    struct Slot { uint32_t hash; bool active; };
    Slot    slots_[MAX_NAMED_BRANCHES] = {};
    uint8_t count_ = 0;
};
