#pragma once
#include <cstdint>

// ── AlienConfigurationType ────────────────────────────────────────────────────
// MD ALIEN_CONFIGURATION_TYPE — maps to DATA/ALIENCONFIGS/*.BML presets.
// DirectorSystem selects the active preset based on gameplay phase.
enum class AlienConfigurationType : uint8_t {
    Default           = 0,
    Mild              = 1,
    Moderate          = 2,
    Intense           = 3,
    BackstageHold     = 4,
    ModeratelyIntense = 5,
    BackstageAlert    = 6,
    BackstageHoldClose  = 7,
    BackstageHoldVClose = 8,
    Unknown           = 0xFF,
};

// ── AlienConfigPreset ─────────────────────────────────────────────────────────
// POD parameter block extracted from ALIENCONFIGS/*.BML files.
// Values verified against DEFAULT.BML (DEFAULT), MILD.BML (MILD), INTENSE.BML (INTENSE).
// DirectorSystem references the active preset for menace/role/sweep tuning.
struct AlienConfigPreset {
    AlienConfigurationType type;
    uint8_t  max_menaces;                       // max simultaneous menace sources
    uint8_t  _pad[2];
    float    menace_gauge_seconds_to_fill;       // seconds to fill menace gauge from 0→1
    float    menace_cool_down_time;              // seconds before menace gauge decays
    float    role_timeout_min;                   // min seconds before role claim times out
    float    role_timeout_max;                   // max seconds before role claim times out
    float    vent_attract_time_min;              // min seconds before NPC investigates vent
    float    vent_attract_time_max;              // max seconds
    float    sweep_box_half_width;               // AreaSweep box half-width (meters)
    float    sweep_box_min_half_length;          // AreaSweep box min half-length (meters)
    float    ambush_timeout;                     // seconds before ambush attempt expires
    float    killtrap_time;                      // seconds for corpse-trap setup
    float    menace_deemed_time;                 // seconds item is "deemed" a menace
    float    max_distance;                       // max sweep distance (meters)
    float    min_distance;                       // min sweep distance (meters)
    float    max_idle_time;                      // max idle wait between sweeps (seconds)
    float    min_idle_time;                      // min idle wait between sweeps (seconds)

    static const AlienConfigPreset& Get(AlienConfigurationType t) noexcept;
};
static_assert(sizeof(AlienConfigPreset) == 64, "AlienConfigPreset must be 64 bytes");

// ── Preset table (BML values) ─────────────────────────────────────────────────
// Inline to avoid a separate .cpp for a pure-data table.
inline const AlienConfigPreset& AlienConfigPreset::Get(AlienConfigurationType t) noexcept {
    static const AlienConfigPreset table[] = {
        // DEFAULT  —  menace_fill, cooldown, role_min, role_max, vent_min, vent_max,
        //             sweep_w, sweep_l, ambush, killtrap, deemed, max_dist, min_dist, max_idle, min_idle
        { AlienConfigurationType::Default, 4, {}, 30.f, 60.f, 45.f, 60.f, 15.f, 20.f,
          8.f, 7.f, 120.f, 3.f, 16.f, 50.f, 10.f, 40.f, 25.f },
        // MILD
        { AlienConfigurationType::Mild, 2, {}, 180.f, 100.f, 45.f, 60.f, 15.f, 20.f,
          8.f, 7.f, 120.f, 3.f, 12.f, 50.f, 10.f, 50.f, 30.f },
        // MODERATE
        { AlienConfigurationType::Moderate, 3, {}, 60.f, 70.f, 45.f, 60.f, 12.f, 18.f,
          8.f, 7.f, 120.f, 3.f, 14.f, 50.f, 10.f, 45.f, 28.f },
        // INTENSE
        { AlienConfigurationType::Intense, 5, {}, 120.f, 80.f, 35.f, 55.f, 10.f, 15.f,
          10.f, 8.f, 90.f, 3.f, 13.f, 60.f, 8.f, 30.f, 20.f },
        // BACKSTAGEHOLD
        { AlienConfigurationType::BackstageHold, 2, {}, 240.f, 120.f, 60.f, 90.f, 20.f, 30.f,
          10.f, 8.f, 180.f, 5.f, 20.f, 40.f, 15.f, 60.f, 35.f },
        // MODERATELY_INTENSE
        { AlienConfigurationType::ModeratelyIntense, 4, {}, 90.f, 75.f, 40.f, 58.f, 12.f, 18.f,
          9.f, 7.f, 100.f, 3.f, 14.f, 55.f, 9.f, 38.f, 24.f },
        // BACKSTAGEALERT
        { AlienConfigurationType::BackstageAlert, 3, {}, 60.f, 60.f, 45.f, 60.f, 15.f, 20.f,
          8.f, 7.f, 90.f, 3.f, 13.f, 50.f, 10.f, 40.f, 25.f },
        // BACKSTAGEHOLD_CLOSE
        { AlienConfigurationType::BackstageHoldClose, 2, {}, 180.f, 100.f, 50.f, 70.f, 18.f, 25.f,
          8.f, 6.f, 150.f, 4.f, 18.f, 35.f, 12.f, 55.f, 32.f },
        // BACKSTAGEHOLD_VCLOSE
        { AlienConfigurationType::BackstageHoldVClose, 2, {}, 150.f, 90.f, 45.f, 65.f, 15.f, 22.f,
          7.f, 5.f, 120.f, 3.f, 16.f, 30.f, 10.f, 50.f, 30.f },
    };
    uint8_t idx = static_cast<uint8_t>(t);
    if (idx >= 9u) return table[0];  // fallback to DEFAULT
    return table[idx];
}
