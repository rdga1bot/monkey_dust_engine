#pragma once
#include <entt/entt.hpp>

// PowerSlotManager — 4 power slots per entity with per-slot cooldowns.
// Slot indices: 0–3. power_id=-1 means empty slot.
// now_s: current game time in seconds (caller tracks this).
namespace md {

class PowerSlotManager {
public:
    static PowerSlotManager& Get() { static PowerSlotManager i; return i; }

    // Assign power_id to slot (0-3). Use -1 to clear.
    void  Assign(entt::entity e, int slot, int power_id);
    int   GetSlot(entt::entity e, int slot) const;

    // Returns remaining cooldown in seconds (0 = ready).
    float CooldownRemaining(entt::entity e, int slot, float now_s) const;

    // Activate slot: validates cooldown, calls PowerSystem::Use, records timestamp.
    // Returns true if the power was successfully used.
    bool  Use(entt::entity e, int slot, float tx, float tz, float now_s);

    void  Clear();

private:
    PowerSlotManager() = default;

    static constexpr int MAX_TRACKED = 64;
    static constexpr int NUM_SLOTS   = 4;

    struct EntitySlots {
        entt::entity entity     = entt::null;
        int          power_ids[NUM_SLOTS]  = {-1, -1, -1, -1};
        float        last_use_s[NUM_SLOTS] = {};
    };

    EntitySlots  entries_[MAX_TRACKED] = {};
    int          count_ = 0;

    EntitySlots*       Find(entt::entity e);
    const EntitySlots* Find(entt::entity e) const;
    EntitySlots*       FindOrCreate(entt::entity e);
};

} // namespace md
