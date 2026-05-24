#pragma once
#include <cstdint>

// Skill indices — Kenshi RE: 25 skills (v11)
enum class Skill : uint8_t {
    // Core (0-4)
    Strength    =  0,
    Dexterity   =  1,
    Athletics   =  2,
    Toughness   =  3,
    Perception  =  4,
    // Labour (5-11)
    Labouring   =  5,
    Science     =  6,
    Engineering =  7,
    Thievery    =  8,
    Farming     =  9,
    Cooking     = 10,
    Hackers     = 11,
    // Combat — weapons (12-18)
    Blunt       = 12,
    Crossbows   = 13,
    Sabre       = 14,
    Dodge       = 15,
    Katana      = 16,
    Crafting    = 17,
    Climbing    = 18,
    // Combat — new v11 (19-24)
    MeleeAttack = 19,
    MeleeDefence= 20,
    Polearm     = 21,
    HeavyWeapons= 22,
    FieldMedic  = 23,
    Mining      = 24,
    COUNT       = 25
};

// ── TraitBitmask — personality traits (bits 0..15) ───────────────────────────
// Kenshi RE: each NPC can have multiple traits that modify behaviour and stats.
namespace Trait {
    constexpr uint16_t Brave     = 1u << 0;  // -fear threshold (less likely to flee)
    constexpr uint16_t Cowardly  = 1u << 1;  // +fear threshold
    constexpr uint16_t Dextrous  = 1u << 2;  // +5 Dexterity
    constexpr uint16_t Clumsy    = 1u << 3;  // -5 Dexterity
    constexpr uint16_t Athletic  = 1u << 4;  // +5 Athletics
    constexpr uint16_t Slow      = 1u << 5;  // -5 Athletics
    constexpr uint16_t ToughSkin = 1u << 6;  // +5 Toughness
    constexpr uint16_t Fragile   = 1u << 7;  // -10 Toughness
    constexpr uint16_t Perceptive= 1u << 8;  // +5 Perception
    constexpr uint16_t Oblivious = 1u << 9;  // -10 Perception
    constexpr uint16_t Ambitious = 1u << 10; // +10% XP gain (all skills)
    constexpr uint16_t Lazy      = 1u << 11; // -10% XP gain
    constexpr uint16_t Bloodlust = 1u << 12; // +10% attack speed modifier
    constexpr uint16_t Pacifist  = 1u << 13; // -10% attack speed modifier
    // bits 14-15 reserved
}

struct StatSheet {
    uint8_t  skills[(int)Skill::COUNT] = {};
    uint8_t  race_id    = 0;      // index into RaceDatabase
    uint8_t  _pad       = 0;
    uint16_t traits     = 0;      // bitmask of Trait:: constants

    uint8_t& operator[](Skill s)       { return skills[(int)s]; }
    uint8_t  operator[](Skill s) const { return skills[(int)s]; }

    // Backward-compat accessors
    uint8_t& strength()   { return skills[(int)Skill::Strength];  }
    uint8_t& dexterity()  { return skills[(int)Skill::Dexterity]; }
    uint8_t& endurance()  { return skills[(int)Skill::Toughness]; }
    uint8_t& melee()      { return skills[(int)Skill::MeleeAttack]; }
    uint8_t& field_medic(){ return skills[(int)Skill::FieldMedic];  }

    bool HasTrait(uint16_t t) const noexcept { return (traits & t) != 0; }
    void AddTrait(uint16_t t) noexcept       { traits |= t; }
    void RemoveTrait(uint16_t t) noexcept    { traits &= (uint16_t)~t; }

    // XP multiplier from traits (Ambitious=+10%, Lazy=-10%).
    float TraitXpMult() const noexcept {
        float m = 1.f;
        if (traits & Trait::Ambitious) m += 0.10f;
        if (traits & Trait::Lazy)      m -= 0.10f;
        return (m < 0.1f) ? 0.1f : m;
    }

    static StatSheet MakeDefault() noexcept {
        StatSheet s{};
        for (auto& v : s.skills) v = 10;
        return s;
    }
};
