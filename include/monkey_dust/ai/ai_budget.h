#pragma once
// AIBudget (VBfA-AI3) — per-logic-tick BT execution budget.
//
// Source: Viking Battle for Asgard character attributes (Absolute_Battle_Factor).
// VBfA used faction-specific costs to cap total AI work per tick regardless of
// how many NPCs are in range. Combined with BT LOD tiers (VBfA-AI1) and awareness
// caps (VBfA-AI2), this gives hard bounded AI cost for any NPC count.
//
// Usage (in BTSystem::Tick phase 3, after LOD skip):
//   if (!AIBudget::Get().TryConsume(cost)) return;  // skip BT
//   btc.tree->tick(...);
//
// Usage (at start of logic tick, before BTSystem::Tick):
//   AIBudget::Get().Reset();
//
// Cost scale:
//   0.0  = Player / exempt (never skipped)
//   0.2  = Schedule / vendor NPC (passive, low AI complexity)
//   1.0  = Standard combat NPC (grunt, guard)
//   1.5  = High-complexity NPC (berserker, boss-tier)
//
// budget_per_tick = 60.0: allows 60 standard-cost BT ticks per 100ms tick.
// With VBfA-AI1 LOD already limiting ~88→25 active BT calls for 500 NPCs,
// the budget is a secondary safety net for extreme close-combat pile-ups.

#include <cstdint>

class AIBudget {
public:
    static AIBudget& Get() {
        static AIBudget inst;
        return inst;
    }

    // Reset at the start of each logic tick (before BTSystem::Tick).
    void Reset() noexcept { used_ = 0.f; }

    // Returns true and consumes cost if budget remains; false = skip this entity's BT.
    // cost=0.0: always succeeds (player / exempt NPCs).
    bool TryConsume(float cost) noexcept {
        if (cost <= 0.f) return true;                    // exempt
        if (used_ + cost > budget_per_tick) return false; // exhausted
        used_ += cost;
        return true;
    }

    float Used()   const noexcept { return used_; }
    float Remaining() const noexcept {
        float r = budget_per_tick - used_;
        return r > 0.f ? r : 0.f;
    }

    // Tunable: lower for slow CPUs, raise for stronger hardware.
    float budget_per_tick = 60.f;

private:
    AIBudget() = default;
    float used_ = 0.f;
};

// ── NPC cost table (VBfA Absolute_Battle_Factor × 60/tick normalised) ────────
namespace BtBudgetCost {
    static constexpr float kPlayer        = 0.0f;  // always ticks
    static constexpr float kScheduleNpc   = 0.2f;  // bt_template_id==255 vendors
    static constexpr float kVendor        = 0.3f;
    static constexpr float kGuard         = 1.0f;  // default combat NPC
    static constexpr float kBerserker     = 1.5f;  // high-priority / boss tier
}
