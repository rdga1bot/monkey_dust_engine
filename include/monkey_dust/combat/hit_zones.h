#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────
// HitZones — сегментовані зони влучання.
//
// Кожна зона має:
//   mult        — множник пошкодження
//   hit_chance  — вірогідність потрапити (0.0–1.0)
//   cripple_hp  — поріг HP зони після якого вона "знята"
//
// Стан зон зберігається у CombatComponent (не тут).
// ─────────────────────────────────────────────────────────

enum class HitZone : uint8_t {
    Head   = 0,  // 1.5× damage, 10% chance, cripple = instant KO
    Torso  = 1,  // 1.0× damage, 55% chance, cripple = death
    LeftArm  = 2,  // 0.8× damage, 10% chance, cripple = -1 hand
    RightArm = 3,  // 0.8× damage, 10% chance, cripple = -weapon
    LeftLeg  = 4,  // 0.9× damage, 7% chance,  cripple = limp
    RightLeg = 5,  // 0.9× damage, 8% chance,  cripple = crawl
    COUNT    = 6
};

struct HitZoneInfo {
    float    damage_mult;
    float    hit_chance;    // сума всіх = 1.0
    float    cripple_hp;    // HP зони при якому вона виходить з ладу
};

// Таблиця зон (індекс = HitZone enum)
constexpr HitZoneInfo ZONE_TABLE[(int)HitZone::COUNT] = {
    { 1.5f, 0.10f, 15.0f },  // Head
    { 1.0f, 0.55f, 40.0f },  // Torso
    { 0.8f, 0.10f, 20.0f },  // LeftArm
    { 0.8f, 0.10f, 20.0f },  // RightArm
    { 0.9f, 0.07f, 25.0f },  // LeftLeg
    { 0.9f, 0.08f, 25.0f },  // RightLeg
};

// Вибрати зону за псевдорандомом (зважена вибірка за hit_chance)
inline HitZone RollHitZone(unsigned int& rng_state) {
    rng_state = rng_state * 1664525u + 1013904223u;
    float roll = (float)(rng_state >> 8) / (float)(1 << 24);  // 0.0–1.0

    float cumulative = 0.0f;
    for (int i = 0; i < (int)HitZone::COUNT; ++i) {
        cumulative += ZONE_TABLE[i].hit_chance;
        if (roll < cumulative)
            return (HitZone)i;
    }
    return HitZone::Torso;
}
