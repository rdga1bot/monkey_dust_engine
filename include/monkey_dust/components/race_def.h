#pragma once
// race_def.h — Kenshi-inspired race system with per-skill modifiers.

#include <monkey_dust/components/stat_sheet.h>
#include <cstring>

enum class RaceType : uint8_t {
    Greenlander  = 0,
    Scorchlander = 1,
    Shek         = 2,
    HiveWorker   = 3,
    HiveSoldier  = 4,
    Skeleton     = 5,
    COUNT        = 6
};

namespace RaceFlag {
    constexpr uint8_t NoHunger   = 1u << 0;  // no hunger accumulation (Skeleton)
    constexpr uint8_t NoFatigue  = 1u << 1;  // no fatigue accumulation (Skeleton)
    constexpr uint8_t NeedRepair = 1u << 2;  // heals via repair not medic (Skeleton)
}

struct RaceDef {
    RaceType type;
    char     name[24];
    int8_t   skill_bonus[(int)Skill::COUNT]; // per-skill bonus/penalty
    uint8_t  flags;
    int8_t   combat_skill_bonus = 0;  // flat bonus to all combat skills (Kenshi RE: +0x244/4)
    uint8_t  _pad[1];
    // Kenshi RE: race mults loaded at +0x47, +0x48, +0x49; stealth guard: <=0 && !=0 → 1.0f
    float    stealth_mult       = 1.f;
    float    athletics_mult     = 1.f;
    float    combat_speed_mult  = 1.f;
};

// Build race table once at first call; returns pointer to static array.
inline const RaceDef* GetRaceDefs() noexcept {
    static RaceDef defs[(int)RaceType::COUNT];
    static bool init = false;
    if (init) return defs;
    init = true;

    memset(defs, 0, sizeof(defs));

    // Greenlander — baseline, no bonuses
    defs[(int)RaceType::Greenlander].type = RaceType::Greenlander;
    strncpy(defs[(int)RaceType::Greenlander].name, "Greenlander", 23);

    // Scorchlander — +10 Perception, +5 Thievery
    defs[(int)RaceType::Scorchlander].type = RaceType::Scorchlander;
    strncpy(defs[(int)RaceType::Scorchlander].name, "Scorchlander", 23);
    defs[(int)RaceType::Scorchlander].skill_bonus[(int)Skill::Perception] =  10;
    defs[(int)RaceType::Scorchlander].skill_bonus[(int)Skill::Thievery]   =   5;

    // Shek — +20 Strength, +15 Toughness, -15 Thievery, -5 Perception
    defs[(int)RaceType::Shek].type = RaceType::Shek;
    strncpy(defs[(int)RaceType::Shek].name, "Shek", 23);
    defs[(int)RaceType::Shek].skill_bonus[(int)Skill::Strength]   =  20;
    defs[(int)RaceType::Shek].skill_bonus[(int)Skill::Toughness]  =  15;
    defs[(int)RaceType::Shek].skill_bonus[(int)Skill::Thievery]   = -15;
    defs[(int)RaceType::Shek].skill_bonus[(int)Skill::Perception] =  -5;

    // HiveWorker — -20 Strength, +15 Athletics, +20 Thievery
    defs[(int)RaceType::HiveWorker].type = RaceType::HiveWorker;
    strncpy(defs[(int)RaceType::HiveWorker].name, "Hive Worker", 23);
    defs[(int)RaceType::HiveWorker].skill_bonus[(int)Skill::Strength]  = -20;
    defs[(int)RaceType::HiveWorker].skill_bonus[(int)Skill::Athletics] =  15;
    defs[(int)RaceType::HiveWorker].skill_bonus[(int)Skill::Thievery]  =  20;

    // HiveSoldier — +10 MeleeAttack, +10 Toughness
    defs[(int)RaceType::HiveSoldier].type = RaceType::HiveSoldier;
    strncpy(defs[(int)RaceType::HiveSoldier].name, "Hive Soldier", 23);
    defs[(int)RaceType::HiveSoldier].skill_bonus[(int)Skill::MeleeAttack] = 10;
    defs[(int)RaceType::HiveSoldier].skill_bonus[(int)Skill::Toughness]   = 10;

    // Skeleton — +10 Strength; no hunger, no fatigue, repair-only healing
    defs[(int)RaceType::Skeleton].type  = RaceType::Skeleton;
    strncpy(defs[(int)RaceType::Skeleton].name, "Skeleton", 23);
    defs[(int)RaceType::Skeleton].skill_bonus[(int)Skill::Strength] = 10;
    defs[(int)RaceType::Skeleton].flags = RaceFlag::NoHunger | RaceFlag::NoFatigue | RaceFlag::NeedRepair;

    return defs;
}

inline const RaceDef* GetRaceDef(uint8_t race_id) noexcept {
    if (race_id >= (uint8_t)RaceType::COUNT) return nullptr;
    return &GetRaceDefs()[race_id];
}

// Apply race skill bonuses to a StatSheet (clamped 0..99).
// Call once on character creation or after loading race_id into StatSheet.
inline void ApplyRaceModifiers(StatSheet& ss) noexcept {
    const RaceDef* def = GetRaceDef(ss.race_id);
    if (!def) return;
    for (int i = 0; i < (int)Skill::COUNT; ++i) {
        int v = (int)ss.skills[i] + (int)def->skill_bonus[i];
        if (v < 0)  v = 0;
        if (v > 99) v = 99;
        ss.skills[i] = (uint8_t)v;
    }
}

// Validate a RaceDef after loading from config.
// Kenshi RE: stealth_mult <= 0 && != 0.0 → forced to 1.0f (no negative stealth penalty).
inline void ValidateRaceDef(RaceDef& def) noexcept {
    if (def.stealth_mult <= 0.f && def.stealth_mult != 0.f) def.stealth_mult = 1.f;
    if (def.athletics_mult  <= 0.f) def.athletics_mult  = 1.f;
    if (def.combat_speed_mult <= 0.f) def.combat_speed_mult = 1.f;
}

inline bool RaceHasFlag(uint8_t race_id, uint8_t flag) noexcept {
    const RaceDef* def = GetRaceDef(race_id);
    return def && (def->flags & flag) != 0;
}
