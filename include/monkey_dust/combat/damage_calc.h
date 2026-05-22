#pragma once
#include <cstdint>
#include <monkey_dust/ecs/registry.h>

// ─────────────────────────────────────────────────────────
// DamageCalc — розрахунок пошкоджень.
//
// Три типи:
//   BLUNT — тупий удар. Добре проти броні, але менше кровотечі.
//           Формула: ефективне = raw * (1 - armor_blunt_resist)
//   CUT   — ріжучий. Середньо проти броні, кровотеча.
//           Формула: ефективне = raw * (1 - armor_cut_resist * 0.5)
//   PIERCE— колючий. Частково ігнорує броню (20% пробиття).
//           Формула: ефективне = raw * (1 - armor_pierce_resist * 0.8)
//
// armor_*_resist: 0.0 = немає захисту, 1.0 = повний імунітет
// ─────────────────────────────────────────────────────────

enum class DamageType : uint8_t {
    Blunt  = 0,
    Cut    = 1,
    Pierce = 2
};

// Kenshi: armour_grade scales resist values (0=poor quality, 1=masterwork).
// Kenshi: armour_penetration on weapons bypasses a fraction of armour (0=none, 1=full bypass).
struct ArmorStats {
    float    blunt_resist;       // 0.0 – 0.9
    float    cut_resist;
    float    pierce_resist;
    float    armour_grade = 1.f; // Kenshi armour grade: multiplies all resist values
    uint16_t hit_sfx_id   = 0;  // AudioSystem SFX slot played on hit (0 = silent)
};

struct WeaponStats {
    float       damage;
    DamageType  type;
    float       attack_range;         // метри
    uint32_t    attack_ms;            // мілісекунди між ударами
    float       armour_penetration = 0.f; // Kenshi: 0=no pen, 1=ignore armour entirely
};

// Набори зброї (data-driven в майбутньому, зараз константи)
namespace Weapons {
    constexpr WeaponStats Fists   = { 8.0f,  DamageType::Blunt,  1.2f, 1200, 0.00f };
    constexpr WeaponStats Sword   = { 28.0f, DamageType::Cut,    1.5f,  900, 0.05f };
    constexpr WeaponStats Spear   = { 22.0f, DamageType::Pierce, 2.2f, 1100, 0.20f };
    constexpr WeaponStats Mace    = { 35.0f, DamageType::Blunt,  1.4f, 1400, 0.10f };
    constexpr WeaponStats Dagger  = { 18.0f, DamageType::Pierce, 1.1f,  700, 0.15f };
}

namespace Armors {
    constexpr ArmorStats None    = { 0.00f, 0.00f, 0.00f, 1.f };
    constexpr ArmorStats Leather = { 0.10f, 0.25f, 0.15f, 1.f };
    constexpr ArmorStats Chain   = { 0.25f, 0.40f, 0.30f, 1.f };
    constexpr ArmorStats Plate   = { 0.50f, 0.55f, 0.45f, 1.f };
}

// Розрахувати ефективне пошкодження з урахуванням типу, броні, grade і penetration.
// effective_resist = base * armour_grade * (1 - armour_penetration)
inline float CalcDamage(const WeaponStats& wpn, const ArmorStats& armor,
                        float hit_zone_mult = 1.0f)
{
    float raw = wpn.damage;
    float pen_factor = 1.0f - wpn.armour_penetration;
    float effective  = 0.0f;

    switch (wpn.type) {
    case DamageType::Blunt:
        effective = raw * (1.0f - armor.blunt_resist * armor.armour_grade * pen_factor);
        break;
    case DamageType::Cut:
        effective = raw * (1.0f - armor.cut_resist * 0.5f * armor.armour_grade * pen_factor);
        break;
    case DamageType::Pierce:
        effective = raw * (1.0f - armor.pierce_resist * 0.8f * armor.armour_grade * pen_factor);
        break;
    }

    effective *= hit_zone_mult;
    return effective < 1.0f ? 1.0f : effective;
}

// VBfA-R: combo and stagger timing per weapon speed class (fast/medium/slow).
struct CombatConfig {
    float combo_window_fast   = 1.000f;  // seconds: combo window for fast weapons
    float combo_window_medium = 1.250f;  // seconds: medium weapons
    float combo_window_slow   = 1.500f;  // seconds: slow/heavy weapons
    float stagger_fast        = 0.333f;  // stagger duration (s) hit by fast weapon
    float stagger_medium      = 0.500f;
    float stagger_slow        = 0.667f;
};

// VBfA-R: AoE damage hit (explosion, heavy slam, grenade).
// Radii from VBfA: SMALL=1.5m, MEDIUM=3m, LARGE=6m.
struct AoeHit {
    float      origin[3];   // world-space centre
    float      radius;      // blast radius (m)
    float      damage;      // base damage at epicentre
    DamageType type;
    uint8_t    _pad[3];
};
namespace AoeRadius {
    static constexpr float Small  = 1.5f;
    static constexpr float Medium = 3.0f;
    static constexpr float Large  = 6.0f;
}

// Apply AoE damage to all entities with LimbHealth + WorldTransform within radius.
// Damage falls off linearly from epicentre to edge. Hits torso limb.
void AoeApply(entt::registry& reg, const AoeHit& h);

