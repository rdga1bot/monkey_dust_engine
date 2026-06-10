#pragma once
// BleedComponent — Kenshi-style damage-over-time bleeding.
//
// bleed_rate:  HP/s applied as ongoing damage while active.
// clot_rate:   HP/s recovered after active=false (wound closing).
// accum:       sub-tick fractional accumulator; game CombatSystem applies floor(accum).
// active:      true while wound is open; game sets false when NPC stops taking hits.
//
// Processing: game/src/combat/combat_system.cpp or a dedicated bleed_tick.cpp
//   each LOGIC_TICK_S:
//     if (bc.active) { bc.accum += bc.bleed_rate * LOGIC_TICK_S; apply floor(accum); accum -= floor(accum); }
//     else if (bc.accum > 0) { bc.accum = max(0, bc.accum - bc.clot_rate * LOGIC_TICK_S); }

#include <cstdint>

struct BleedComponent {
    float    bleed_rate  = 2.0f;  // HP/s while bleeding (Kenshi default: ~2 HP/s)
    float    clot_rate   = 0.5f;  // HP/s clot recovery when not actively bleeding
    float    accum       = 0.0f;  // fractional accumulator; apply floor each tick
    // Kenshi RE: haemorrhage_shock — one-shot HP loss applied on the next tick then zeroed.
    // Represents immediate blood loss from a cut (20% of cut damage, applied instantly).
    float    immediate   = 0.0f;
    bool     active      = false; // true = wound open, taking damage; false = clotting
    uint8_t  bleed_zone  = 1;     // LimbHealth index of the wounded limb (default: Torso=1)
    uint8_t  _pad[2]     = {};
};
static_assert(sizeof(BleedComponent) == 20, "BleedComponent must be 20 bytes");
