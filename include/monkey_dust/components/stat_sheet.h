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

struct StatSheet {
    uint8_t skills[(int)Skill::COUNT] = {};

    uint8_t& operator[](Skill s)       { return skills[(int)s]; }
    uint8_t  operator[](Skill s) const { return skills[(int)s]; }

    // Backward-compat accessors
    uint8_t& strength()   { return skills[(int)Skill::Strength];  }
    uint8_t& dexterity()  { return skills[(int)Skill::Dexterity]; }
    uint8_t& endurance()  { return skills[(int)Skill::Toughness]; }
    uint8_t& melee()      { return skills[(int)Skill::MeleeAttack]; }
    uint8_t& field_medic(){ return skills[(int)Skill::FieldMedic];  }

    static StatSheet MakeDefault() noexcept {
        StatSheet s{};
        for (auto& v : s.skills) v = 10;
        return s;
    }
};
