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
    Pierce = 2,
    Ranged = 3,  // bow/crossbow — Kenshi RE: bow_damage_1/bow_damage_99
    COUNT  = 4
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
// skill_level [0..100]: Kenshi Phase 2, Task 2.8 — damage × (1 + skill × 0.01)
// effective_resist = base * armour_grade * (1 - armour_penetration)
inline float CalcDamage(const WeaponStats& wpn, const ArmorStats& armor,
                        float hit_zone_mult = 1.0f, int skill_level = 0)
{
    float raw = wpn.damage * (1.0f + (float)skill_level * 0.01f);
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
    case DamageType::Ranged:
        effective = raw * (1.0f - armor.pierce_resist * 0.8f * armor.armour_grade * pen_factor);
        break;
    default: break;
    }

    effective *= hit_zone_mult;
    return effective < 1.0f ? 1.0f : effective;
}

// VBfA-R: combo and stagger timing per weapon speed class (fast/medium/slow).
// Kenshi RE (kenshi_x64.exe.c, thunk_FUN_14086b2b0): damage_1/99 lerp by skill,
// ko thresholds by toughness, bleed/clot rates stored 10× in config (×0.1 at load).
struct CombatConfig {
    // Combo / stagger timing
    float combo_window_fast   = 1.000f;
    float combo_window_medium = 1.250f;
    float combo_window_slow   = 1.500f;
    float stagger_fast        = 0.333f;
    float stagger_medium      = 0.500f;
    float stagger_slow        = 0.667f;

    // Kenshi damage lerp: actual = lerp(damage_1[type], damage_99[type], skill/99.f)
    // Indexed by DamageType (Blunt=0, Cut=1, Pierce=2, Ranged=3)
    float damage_1 [4] = { 8.f, 12.f, 6.f, 10.f };   // damage at skill 1
    float damage_99[4] = { 35.f, 55.f, 30.f, 45.f };  // damage at skill 99
    float pierce_damage_mult = 1.0f; // extra multiplier for stab/pierce hits

    // KO threshold lerp: ko_hp = lerp(ko_min, ko_max, toughness/100.f)
    float ko_threshold_min = 0.10f;  // KO at 10% HP when toughness=0
    float ko_threshold_max = 0.02f;  // KO at 2% HP when toughness=100

    // Defense
    float damage_resistance_min = 0.0f;  // armour minimum resist
    float damage_resistance_max = 0.9f;  // armour maximum resist
    float base_block_chance     = 0.15f; // base parry %
    float block_per_10_levels   = 0.02f; // +2% per 10 skill levels (×0.1 from config)

    // Recovery
    float stun_recovery_rate    = 60.f;  // seconds to recover from stun unaided
    float attack_chance_factor  = 1.0f;  // global hit/miss modifier
    float medic_speed_mult      = 1.0f;  // surgery/bandaging speed multiplier
    float heal_rate_mult        = 1.0f;  // base healing per tick
    float resting_heal_rate_mult= 2.0f;  // healing when sleeping
};

// KO HP threshold for given toughness [0..100].
inline float KoThreshold(const CombatConfig& cfg, int toughness) noexcept {
    float t = (float)toughness * 0.01f;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return cfg.ko_threshold_min + (cfg.ko_threshold_max - cfg.ko_threshold_min) * t;
}

// Kenshi-style damage lerp by skill level (0..99).
// type indexed as (int)DamageType.
inline float CalcDamageLerped(const CombatConfig& cfg, DamageType type,
                               int skill_level) noexcept {
    int ti = (int)type;
    if (ti < 0 || ti >= 4) ti = 0;
    float t = (float)skill_level / 99.f;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    float d = cfg.damage_1[ti] + (cfg.damage_99[ti] - cfg.damage_1[ti]) * t;
    if (type == DamageType::Pierce) d *= cfg.pierce_damage_mult;
    return d;
}

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

