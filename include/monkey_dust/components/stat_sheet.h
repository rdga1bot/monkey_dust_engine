#pragma once
#include <cstdint>

// Skill indices — Kenshi RE: 19 confirmed skills
enum class Skill : uint8_t {
    Strength    =  0,
    Dexterity   =  1,
    Athletics   =  2,
    Toughness   =  3,
    Perception  =  4,
    Labouring   =  5,
    Science     =  6,
    Engineering =  7,
    Thievery    =  8,
    Farming     =  9,
    Cooking     = 10,
    Hackers     = 11,
    Blunt       = 12,
    Crossbows   = 13,
    Sabre       = 14,
    Dodge       = 15,
    Katana      = 16,
    Crafting    = 17,
    Climbing    = 18,
    COUNT       = 19
};

struct StatSheet {
    uint8_t skills[(int)Skill::COUNT] = {};

    uint8_t& operator[](Skill s)       { return skills[(int)s]; }
    uint8_t  operator[](Skill s) const { return skills[(int)s]; }

    // Backward-compat accessors
    uint8_t& strength()  { return skills[(int)Skill::Strength];  }
    uint8_t& dexterity() { return skills[(int)Skill::Dexterity]; }
    uint8_t& endurance() { return skills[(int)Skill::Toughness]; }

    static StatSheet MakeDefault() noexcept {
        StatSheet s{};
        for (auto& v : s.skills) v = 10;
        return s;
    }
};
