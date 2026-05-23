#pragma once
#include <cstdint>

// ── AlienActionType ───────────────────────────────────────────────────────────
// Discrete AI actions the alien NPC may attempt; stored as bit positions in
// AlienConfigPreset::allowed_actions. ConditionAlienIsAllowed checks the active
// preset to gate BT branches to permitted strategies.
enum class AlienActionType : uint8_t {
    ThreatAware              = 1,  // patrol with heightened threat awareness
    WillKilltrap             = 3,  // set up a corpse trap / killtrap
    WillFlankFromThreatAware = 5,  // attempt flanking maneuver from aware state
    WillAmbush               = 6,  // hide and ambush target
};

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
    // ── Kenshi fauna presets (Phase 4, Task 4.8) ──────────────────────────────
    BeakThing         = 9,   // Large aggressive predator — max menace, wide sweep
    Bull              = 10,  // Stampeding herd — charges in straight line, no ambush
    Leviathan         = 11,  // Massive passive — slow, ignores player unless attacked
    Garru             = 12,  // Pack animal — timid, flees on threat
    Bonedog           = 13,  // Scavenger — moderate aggression, hunts wounded
    Unknown           = 0xFF,
};

// ── AlienConfigPreset ─────────────────────────────────────────────────────────
// POD parameter block extracted from ALIENCONFIGS/*.BML files.
// Values verified against DEFAULT.BML (DEFAULT), MILD.BML (MILD), INTENSE.BML (INTENSE).
// DirectorSystem references the active preset for menace/role/sweep tuning.
struct AlienConfigPreset {
    AlienConfigurationType type;
    uint8_t  max_menaces;                       // max simultaneous menace sources
    uint16_t allowed_actions;                   // bitmask: (1<<AlienActionType) per allowed action
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
    // allowed_actions bitmask: bit positions from AlienActionType values
    //   ThreatAware=1→bit1=2, WillKilltrap=3→bit3=8,
    //   WillFlankFromThreatAware=5→bit5=32, WillAmbush=6→bit6=64
    static const AlienConfigPreset table[] = {
        // DEFAULT — all four actions permitted (2+8+32+64=106)
        { AlienConfigurationType::Default, 4, 106u, 30.f, 60.f, 45.f, 60.f, 15.f, 20.f,
          8.f, 7.f, 120.f, 3.f, 16.f, 50.f, 10.f, 40.f, 25.f },
        // MILD — ThreatAware only (2)
        { AlienConfigurationType::Mild, 2, 2u, 180.f, 100.f, 45.f, 60.f, 15.f, 20.f,
          8.f, 7.f, 120.f, 3.f, 12.f, 50.f, 10.f, 50.f, 30.f },
        // MODERATE — ThreatAware + WillAmbush (2+64=66)
        { AlienConfigurationType::Moderate, 3, 66u, 60.f, 70.f, 45.f, 60.f, 12.f, 18.f,
          8.f, 7.f, 120.f, 3.f, 14.f, 50.f, 10.f, 45.f, 28.f },
        // INTENSE — all four actions (106)
        { AlienConfigurationType::Intense, 5, 106u, 120.f, 80.f, 35.f, 55.f, 10.f, 15.f,
          10.f, 8.f, 90.f, 3.f, 13.f, 60.f, 8.f, 30.f, 20.f },
        // BACKSTAGEHOLD — ThreatAware only (2)
        { AlienConfigurationType::BackstageHold, 2, 2u, 240.f, 120.f, 60.f, 90.f, 20.f, 30.f,
          10.f, 8.f, 180.f, 5.f, 20.f, 40.f, 15.f, 60.f, 35.f },
        // MODERATELY_INTENSE — ThreatAware + WillFlank + WillAmbush (2+32+64=98)
        { AlienConfigurationType::ModeratelyIntense, 4, 98u, 90.f, 75.f, 40.f, 58.f, 12.f, 18.f,
          9.f, 7.f, 100.f, 3.f, 14.f, 55.f, 9.f, 38.f, 24.f },
        // BACKSTAGEALERT — ThreatAware + WillAmbush (2+64=66)
        { AlienConfigurationType::BackstageAlert, 3, 66u, 60.f, 60.f, 45.f, 60.f, 15.f, 20.f,
          8.f, 7.f, 90.f, 3.f, 13.f, 50.f, 10.f, 40.f, 25.f },
        // BACKSTAGEHOLD_CLOSE — ThreatAware only (2)
        { AlienConfigurationType::BackstageHoldClose, 2, 2u, 180.f, 100.f, 50.f, 70.f, 18.f, 25.f,
          8.f, 6.f, 150.f, 4.f, 18.f, 35.f, 12.f, 55.f, 32.f },
        // BACKSTAGEHOLD_VCLOSE — ThreatAware only (2)
        { AlienConfigurationType::BackstageHoldVClose, 2, 2u, 150.f, 90.f, 45.f, 65.f, 15.f, 22.f,
          7.f, 5.f, 120.f, 3.f, 16.f, 30.f, 10.f, 50.f, 30.f },
        // BEAK_THING — all actions, fast fill, large sweep (apex predator)
        { AlienConfigurationType::BeakThing, 6, 106u, 15.f, 30.f, 30.f, 45.f, 8.f, 12.f,
          15.f, 10.f, 60.f, 2.f, 10.f, 80.f, 5.f, 20.f, 10.f },
        // BULL — ThreatAware + WillFlank (34=2+32): charges straight, no killtrap/ambush
        { AlienConfigurationType::Bull, 3, 34u, 20.f, 40.f, 40.f, 60.f, 5.f, 8.f,
          20.f, 15.f, 90.f, 0.f, 8.f, 100.f, 20.f, 15.f, 8.f },
        // LEVIATHAN — ThreatAware only (2), extremely slow, huge range
        { AlienConfigurationType::Leviathan, 1, 2u, 600.f, 300.f, 120.f, 180.f, 60.f, 90.f,
          40.f, 30.f, 600.f, 0.f, 60.f, 200.f, 50.f, 120.f, 80.f },
        // GARRU — ThreatAware only, high drain (flees quickly), small sweep
        { AlienConfigurationType::Garru, 1, 2u, 300.f, 60.f, 60.f, 90.f, 10.f, 15.f,
          4.f, 3.f, 60.f, 0.f, 5.f, 20.f, 5.f, 60.f, 40.f },
        // BONEDOG — ThreatAware + WillAmbush (66): scavenger, hunts wounded
        { AlienConfigurationType::Bonedog, 3, 66u, 40.f, 50.f, 35.f, 55.f, 10.f, 15.f,
          8.f, 6.f, 90.f, 2.f, 12.f, 40.f, 8.f, 35.f, 22.f },
    };
    uint8_t idx = static_cast<uint8_t>(t);
    if (idx >= 14u) return table[0];  // fallback to DEFAULT
    return table[idx];
}
