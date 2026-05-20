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
    float   attack_range;     // Batch 33: effective range (metres); game CombatSystem writes at spawn/equip
};
static_assert(sizeof(WeaponComponent) == 8, "WeaponComponent must be 8 bytes");
