#pragma once
// NpcNeeds — Kenshi-style survival needs for NPCs.
// Updated by NpcNeedsSystem::Tick() at 1 Hz (world simulation rate).
// BT can read these via AgentBlackboard or direct component access.
//
// Kenshi RE reference:
//   char+0x48 = hunger_level (float, 0=full, 1=starving)
//   char+0x4c = fatigue_level (float, 0=rested, 1=exhausted)
//   Food item consumption restores hunger; sleep action restores fatigue.
#include <cstdint>

struct NpcNeeds {
    float hunger   = 0.f;    // [0, 1]: 0=full, 1=starving; increases ~0.002/s
    float fatigue  = 0.f;    // [0, 1]: 0=rested, 1=exhausted; increases ~0.001/s
    float morale   = 1.f;    // [0, 1]: mood/morale; decreases from hunger+fatigue
    uint8_t hungry_thresh  = 200; // scaled: 200/255 ≈ 0.78 → NPC seeks food
    uint8_t sleepy_thresh  = 220; // scaled: 220/255 ≈ 0.86 → NPC seeks sleep
    uint8_t is_sleeping    = 0;   // hysteresis: 1 while sleeping until fatigue<FATIGUE_SLEEP_DONE
    uint8_t _pad           = 0;
};
static_assert(sizeof(NpcNeeds) == 16, "NpcNeeds must be 16 bytes");

// SurvivalConfig — global balance constants for the survival simulation.
// Loaded from config once; not per-entity. Kenshi RE: thunk_FUN_14086b2b0 offsets.
struct SurvivalConfig {
    // Hunger / starvation
    float starvation_time_s        = 86400.f; // seconds until death from starvation (~24 h)
    float fed_recovery_rate_mult   = 1.5f;    // recovery speed multiplier when fed
    float bed_hunger_rate          = 0.5f;    // hunger rate modifier when sleeping in bed
    float encumbrance_hunger_rate  = 0.003f;  // extra hunger per kg over carry limit per tick
    float food_quality_mult        = 1.0f;    // quality scaling for food effects

    // Death / respawn
    float death_time_s             = 300.f;   // seconds until permanently dead if not revived
    float death_threshold_frac     = 0.05f;   // blood fraction below which char enters "dying"
    float min_respawn_time_s       = 600.f;   // minimum seconds before faction respawns NPC
    float max_respawn_time_s       = 86400.f; // maximum seconds (1 day)
};

static inline SurvivalConfig& GetSurvivalConfig() noexcept {
    static SurvivalConfig cfg;
    return cfg;
}

// Personality archetype — drives idle behavior preferences (Kenshi RE: character personality).
enum class NpcPersonality : uint8_t {
    Neutral    = 0,
    Aggressive = 1,   // higher base aggression, faster escalation
    Timid      = 2,   // lower flee threshold, runs sooner
    Lazy       = 3,   // slower to help, lower patrol frequency
    Cheerful   = 4,   // morale recovers faster
    Stoic      = 5,   // morale decays slower, ignores minor threats
    COUNT      = 6
};

// Per-entity personality component (optional — attach to give NPC distinct behaviour).
struct NpcPersonalityComp {
    NpcPersonality type      = NpcPersonality::Neutral;
    uint8_t        variation = 128; // 0-255 within-archetype variation
    uint8_t        _pad[6]  = {};
};
static_assert(sizeof(NpcPersonalityComp) == 8, "NpcPersonalityComp must be 8 bytes");
