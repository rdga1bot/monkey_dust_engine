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
// Loaded from config once; not per-entity.
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

// B-3: load SurvivalConfig from a flat JSON file. Silently keeps defaults on error.
// Format: { "starvation_time_s": 86400, "bed_hunger_rate": 0.5, ... }
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <monkey_dust/platform/md_log.h>
inline void LoadSurvivalConfig(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { MD_LOG(MD_LOG_WARNING, "[SurvivalConfig] not found: %s (using defaults)", path); return; }
    static char buf[1024];
    int n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    SurvivalConfig& c = GetSurvivalConfig();
    auto readf = [&](const char* key, float& out) {
        const char* p = strstr(buf, key);
        if (!p) return;
        p += strlen(key);
        while (*p && (*p == '"' || *p == ':' || *p == ' ')) ++p;
        if (*p) out = (float)strtod(p, nullptr);
    };
    readf("starvation_time_s",       c.starvation_time_s);
    readf("fed_recovery_rate_mult",  c.fed_recovery_rate_mult);
    readf("bed_hunger_rate",         c.bed_hunger_rate);
    readf("encumbrance_hunger_rate", c.encumbrance_hunger_rate);
    readf("food_quality_mult",       c.food_quality_mult);
    readf("death_time_s",            c.death_time_s);
    readf("death_threshold_frac",    c.death_threshold_frac);
    readf("min_respawn_time_s",      c.min_respawn_time_s);
    readf("max_respawn_time_s",      c.max_respawn_time_s);
    MD_LOG(MD_LOG_INFO, "[SurvivalConfig] loaded from %s", path);
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
