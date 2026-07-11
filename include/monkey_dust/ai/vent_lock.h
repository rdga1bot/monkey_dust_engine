#pragma once
#include <cstdint>
#include <monkey_dust/ecs/md_entity.h>
#include <entt/entt.hpp>

// ── VentLockTable ─────────────────────────────────────────────────────────────
// Header-only singleton that grants exclusive vent access to one NPC at a time.
// MAX_VENTS = 8 fixed slots; entity holds the lock until Release() is called.
// Used by DecoratorLockVent BT node — lock acquired on entry, released on return.
class VentLockTable {
public:
    static constexpr uint8_t MAX_VENTS = 8;

    static VentLockTable& Get() {
        static VentLockTable instance;
        return instance;
    }

    // Returns true if vent_id is free or already owned by entity (re-entrant).
    bool TryLock(uint8_t vent_id, MdEntity owner) {
        if (vent_id >= MAX_VENTS) return false;
        if (locks_[vent_id] == entt::null || locks_[vent_id] == owner) {
            locks_[vent_id] = owner;
            return true;
        }
        return false;
    }

    // Release all vent slots held by entity.
    void Release(MdEntity owner) {
        for (uint8_t i = 0; i < MAX_VENTS; ++i)
            if (locks_[i] == owner) locks_[i] = entt::null;
    }

    void ClearAll() {
        for (uint8_t i = 0; i < MAX_VENTS; ++i) locks_[i] = entt::null;
    }

private:
    VentLockTable() { ClearAll(); }
    MdEntity locks_[MAX_VENTS];
};
