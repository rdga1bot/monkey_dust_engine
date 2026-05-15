#pragma once
#include <cstdint>

// Per-entity weapon state. Written by inventory/animation systems; BT reads via
// ConditionCurrentWeaponIsEquipped / ConditionCurrentWeaponNeedsReloading /
// ConditionHasMeleeAttackAvailable / ActionWeaponEquip.
struct WeaponComponent {
    bool    is_equipped;      // weapon is drawn and ready
    bool    needs_reload;     // magazine empty — reload required before shooting
    bool    melee_available;  // melee attack cooldown has cleared
    uint8_t weapon_type;      // 0=none 1=pistol 2=shotgun 3=rifle 4=melee
};
static_assert(sizeof(WeaponComponent) == 4, "WeaponComponent must be 4 bytes");
