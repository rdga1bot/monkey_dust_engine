#pragma once
#include <cstdint>
#include <cstring>

// WeatherSystem — Kenshi-style weather simulation.
// Kenshi RE (kenshi_x64.exe.c): "weathers" list, "weather type", "weather strength
// limit min/max", "weather strength multiplier min/max", "weather protection amount",
// "weather protection" ref list, "weather immunity" ref list.
//
// Each zone has a list of possible WeatherDefs; WeatherSystem picks one per day
// and interpolates strength between min/max.

static constexpr int MAX_WEATHER_DEFS    = 32;
static constexpr int MAX_ZONE_WEATHERS   = 8;   // max weather types per zone
static constexpr int MAX_WEATHER_PROTECT = 4;   // item protection types per item

enum class WeatherType : uint8_t {
    None       = 0,
    Clear      = 1,
    Dust       = 2,  // Kenshi: "weather type" dust storm
    Rain       = 3,
    Storm      = 4,
    Snow       = 5,
    Acid       = 6,  // toxic zone weather
    Fog        = 7,
    COUNT      = 8
};

struct WeatherDef {
    WeatherType type;
    uint8_t     _pad[3];
    float       strength_min    = 0.f;   // Kenshi: "weather strength limit min"
    float       strength_max    = 1.f;   // Kenshi: "weather strength limit max"
    float       mult_min        = 1.f;   // Kenshi: "weather strength multiplier min"
    float       mult_max        = 1.f;   // Kenshi: "weather strength multiplier max"
    float       damage_per_s    = 0.f;   // damage to unprotected NPCs per second
    char        name[16];
};  // 40B
static_assert(sizeof(WeatherDef) == 40, "WeatherDef must be 40 bytes");

// Per-item weather protection (Kenshi RE: "weather protection amount" + "weather protection" ref).
struct WeatherProtection {
    WeatherType weather_type = WeatherType::None;
    uint8_t     _pad[3];
    float       protection   = 0.f;  // fraction absorbed [0..1]
};

// WeatherState — current weather for the loaded zone.
struct WeatherState {
    WeatherType active_type   = WeatherType::Clear;
    uint8_t     _pad[3];
    float       strength      = 0.f;   // [0..1], interpolated from def min/max
    float       transition_t  = 0.f;   // seconds elapsed in current weather
    float       duration_s    = 300.f; // target duration before weather change
};
static_assert(sizeof(WeatherState) == 16, "WeatherState must be 16 bytes");

// WeatherSystem singleton — ticks weather transitions.
class WeatherSystem {
public:
    static WeatherSystem& Get() { static WeatherSystem s; return s; }

    // Register a weather def (call at startup for each zone's possible weather).
    void RegisterDef(const WeatherDef& def) {
        if (def_count_ < MAX_WEATHER_DEFS) defs_[def_count_++] = def;
    }

    // Tick weather state (call from WorldSimulation::Tick, 1 Hz).
    void Tick(float delta_s) noexcept {
        state_.transition_t += delta_s;
        if (state_.transition_t >= state_.duration_s) {
            // Pick next weather: very simple cycle (randomize in real impl)
            state_.active_type = WeatherType::Clear;
            state_.strength    = 0.f;
            state_.transition_t= 0.f;
            state_.duration_s  = 300.f + (float)(def_count_ * 60); // vary by available defs
        }
    }

    const WeatherState& State() const noexcept { return state_; }

    // Returns damage per second for current weather, reduced by protection fraction.
    float DamageForNpc(float weather_protection_total) const noexcept {
        const WeatherDef* def = DefFor(state_.active_type);
        if (!def || def->damage_per_s <= 0.f) return 0.f;
        float dmg = def->damage_per_s * state_.strength;
        float prot = weather_protection_total;
        if (prot > 1.f) prot = 1.f;
        return dmg * (1.f - prot);
    }

private:
    WeatherSystem() { memset(defs_, 0, sizeof(defs_)); }

    const WeatherDef* DefFor(WeatherType t) const noexcept {
        for (int i = 0; i < def_count_; ++i)
            if (defs_[i].type == t) return &defs_[i];
        return nullptr;
    }

    WeatherDef  defs_[MAX_WEATHER_DEFS];
    int         def_count_ = 0;
    WeatherState state_;
};
