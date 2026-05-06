#pragma once
#include <cstdint>
#include <algorithm>

// ─────────────────────────────────────────────────────────
// DamageCalc — розрахунок пошкоджень.
//
// Три типи:
//   BLUNT — тупий удар. Добре проти броні, але менше кровотечі.
//           Формула: ефективне = raw * (1 - armor_blunt_resist)
//   CUT   — ріжучий. Середньо проти броні, кровотеча.
//           Формула: ефективне = raw * (1 - armor_cut_resist * 0.7)
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

struct ArmorStats {
    float blunt_resist;   // 0.0 – 0.9
    float cut_resist;
    float pierce_resist;
};

struct WeaponStats {
    float       damage;
    DamageType  type;
    float       attack_range;   // метри
    uint32_t    attack_ms;      // мілісекунди між ударами
};

// Набори зброї (data-driven в майбутньому, зараз константи)
namespace Weapons {
    constexpr WeaponStats Fists   = { 8.0f,  DamageType::Blunt,  1.2f, 1200 };
    constexpr WeaponStats Sword   = { 28.0f, DamageType::Cut,    1.5f,  900 };
    constexpr WeaponStats Spear   = { 22.0f, DamageType::Pierce, 2.2f, 1100 };
    constexpr WeaponStats Mace    = { 35.0f, DamageType::Blunt,  1.4f, 1400 };
    constexpr WeaponStats Dagger  = { 18.0f, DamageType::Pierce, 1.1f,  700 };
}

namespace Armors {
    constexpr ArmorStats None     = { 0.00f, 0.00f, 0.00f };
    constexpr ArmorStats Leather  = { 0.10f, 0.25f, 0.15f };
    constexpr ArmorStats Chain    = { 0.25f, 0.40f, 0.30f };
    constexpr ArmorStats Plate    = { 0.50f, 0.55f, 0.45f };
}

// Розрахувати ефективне пошкодження з урахуванням типу і броні.
inline float CalcDamage(const WeaponStats& wpn, const ArmorStats& armor,
                        float hit_zone_mult = 1.0f)
{
    float raw = wpn.damage;
    float effective = 0.0f;

    switch (wpn.type) {
    case DamageType::Blunt:
        effective = raw * (1.0f - armor.blunt_resist);
        break;
    case DamageType::Cut:
        // Cut ефективніше проти неброньованих (порівняно з Blunt)
        effective = raw * (1.0f - armor.cut_resist * 0.7f);
        break;
    case DamageType::Pierce:
        // Pierce пробиває 20% більше броні
        effective = raw * (1.0f - armor.pierce_resist * 0.8f);
        break;
    }

    effective *= hit_zone_mult;
    // Мінімум 1 пошкодження (завжди щось проходить)
    return effective < 1.0f ? 1.0f : effective;
}
