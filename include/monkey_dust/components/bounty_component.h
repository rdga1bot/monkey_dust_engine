#pragma once
#include <cstdint>

// BountyComponent — Kenshi-style per-NPC bounty tracking.
//
// Kenshi RE spawn logic:
//   if (rand(0,100) < bounty_chance) {
//       amount = base + rand(0, fuzz*2) * 0.5
//       for each faction in bounty_factions → AddBounty(npc, faction, amount)
//   }
//
// MD simplification: single faction_id (0 = no bounty), amount in game currency.
// Bounties are set at NPC spawn and cleared on death/capture/payment.
//
// sizeof = 8 bytes

struct BountyComponent {
    uint32_t faction_id = 0;      // 0 = no active bounty
    uint16_t amount     = 0;      // bounty value in currency units
    uint8_t  chance     = 0;      // spawn chance 0..100 (used at spawn time only)
    uint8_t  flags      = 0;      // bit 0 = captured (bounty redeemable), bit 1 = paid out

    bool HasBounty()   const noexcept { return faction_id != 0 && amount > 0; }
    bool IsCaptured()  const noexcept { return (flags & 1) != 0; }
    void SetCaptured()       noexcept { flags |= 1; }
    void PayOut()            noexcept { amount = 0; faction_id = 0; flags |= 2; }

    // Roll bounty at NPC spawn. base/fuzz in currency units. rand01/rand_fuzz = pre-rolled [0..1].
    // Kenshi formula: amount = base + rand(0, fuzz*2) * 0.5
    // Caller provides random floats to avoid stdlib dependency in header.
    static BountyComponent Roll(uint32_t fac, uint8_t chance,
                                uint16_t base, uint16_t fuzz,
                                float rand_chance_0_1,
                                float rand_fuzz_0_1) noexcept {
        BountyComponent bc;
        if (rand_chance_0_1 * 100.f < (float)chance) {
            int rolled = (int)base + (fuzz > 0 ? (int)(rand_fuzz_0_1 * (float)(fuzz * 2) * 0.5f) : 0);
            bc.faction_id = fac;
            bc.amount     = (uint16_t)(rolled > 0xFFFF ? 0xFFFF : rolled);
            bc.chance     = chance;
        }
        return bc;
    }
};
static_assert(sizeof(BountyComponent) == 8, "BountyComponent must be 8 bytes");
