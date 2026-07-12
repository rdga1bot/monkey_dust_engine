#pragma once
#include <monkey_dust/ecs/md_entity.h>

// PowerSlotManager — 4 power slots per entity with per-slot cooldowns.
// Slot indices: 0–3. power_id=-1 means empty slot.
// now_s: current game time in seconds (caller tracks this).
namespace md {

class PowerSlotManager {
public:
    static PowerSlotManager& Get() { static PowerSlotManager i; return i; }

    // Assign power_id to slot (0-3). Use -1 to clear.
    void  Assign(MdEntity e, int slot, int power_id);
    int   GetSlot(MdEntity e, int slot) const;

    // Returns remaining cooldown in seconds (0 = ready).
    float CooldownRemaining(MdEntity e, int slot, float now_s) const;

    // Activate slot: validates cooldown, calls PowerSystem::Use, records timestamp.
    // Returns true if the power was successfully used.
    bool  Use(MdEntity e, int slot, float tx, float tz, float now_s);

    void  Clear();

private:
    PowerSlotManager() = default;

    static constexpr int MAX_TRACKED = 64;
    static constexpr int NUM_SLOTS   = 4;

    struct EntitySlots {
        MdEntity entity     = MdEntity::Null();
        int          power_ids[NUM_SLOTS]  = {-1, -1, -1, -1};
        float        last_use_s[NUM_SLOTS] = {};
    };

    EntitySlots  entries_[MAX_TRACKED] = {};
    int          count_ = 0;

    EntitySlots*       Find(MdEntity e);
    const EntitySlots* Find(MdEntity e) const;
    EntitySlots*       FindOrCreate(MdEntity e);
};

} // namespace md
