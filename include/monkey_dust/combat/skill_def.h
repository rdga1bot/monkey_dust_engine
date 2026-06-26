#pragma once
#include <cstdint>

// Kenshi-RE skill IDs (RE 2026-06-25, HIGH confidence — complete switch stmt found).
// uint8_t is sufficient: max ID = 65. Gaps are reserved/removed Kenshi internals.
// MAX_SKILL_ID = 65; valid skills fill the non-gap entries.
enum class SkillId : uint8_t {
    // ── Core attributes ──────────────────────────────────────────────────────────
    Strength         =  1,
    Attack           =  2,
    Labouring        =  3,
    Science          =  4,
    Engineering      =  5,
    Robotics         =  6,
    WeaponSmithing   =  7,
    ArmourSmithing   =  8,
    // 9 = unknown / internal
    Thievery         = 10,
    Turrets          = 11,
    Farming          = 12,
    Cooking          = 13,
    // 14-15 = weapon macro category (shared string ptr, not skill rows)
    // ── Combat stats ─────────────────────────────────────────────────────────────
    Stealth          = 16,
    Athletics        = 17,
    Dexterity        = 18,
    Defence          = 19,
    Weaponry         = 20,
    Toughness        = 21,
    Assassination    = 22,
    Swimming         = 23,
    Perception       = 24,
    // ── Weapon skills ────────────────────────────────────────────────────────────
    Katanas          = 25,
    Sabres           = 26,
    Hackers          = 27,
    HeavyWeapons     = 28,
    // 29-31 = removed/deprecated weapon categories
    Dodging          = 32,
    // 33-34 = gap
    Crossbows        = 35,
    PrecisionShooting= 36,
    Lockpicking      = 37,
    CrossbowSmithing = 38,
    // 39-63 = reserved (future or removed Kenshi skills)
    // ── Derived stat namespaces (not learnable) ───────────────────────────────────
    Encumbrance      = 64,
    CombatSpeed      = 65,
};

static constexpr int MAX_SKILL_ID = 65;
static constexpr int MAX_SKILLS   = MAX_SKILL_ID + 1; // index by SkillId directly

// True for IDs that correspond to actual learnable skills (non-gap).
inline bool IsValidSkill(SkillId id) {
    uint8_t v = static_cast<uint8_t>(id);
    if (v == 0 || v > MAX_SKILL_ID) return false;
    // Gaps: 9, 14-15, 29-31, 33-34, 39-63
    if (v == 9)                       return false;
    if (v >= 14 && v <= 15)           return false;
    if (v >= 29 && v <= 31)           return false;
    if (v == 33 || v == 34)           return false;
    if (v >= 39 && v <= 63)           return false;
    return true;
}
