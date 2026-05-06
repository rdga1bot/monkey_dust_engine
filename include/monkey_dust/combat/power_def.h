#pragma once
#include <cstdint>

// Phase 38: runtime power definition loaded from data/flare/powers.json.
// Derived from flare-game Empyrean Campaign (CC BY-SA 3.0).

struct PowerDef {
    int   id;
    char  name[32];
    char  dmg_type[8];  // "melee" / "ranged" / ""
    float radius;       // meters (1 FLARE tile ≈ 1 m)
    int   cooldown_ms;
};
static_assert(sizeof(PowerDef) == 52, "PowerDef layout changed");
