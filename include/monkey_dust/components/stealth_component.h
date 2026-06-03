#pragma once
#include <cstdint>

// StealthComponent — B-1: per-entity stealth state.
// stealth_factor [0..1]: 0=invisible, 1=fully visible. Multiplies SenseComponent
// activation fill rate for AudioMovement and Visual sense types.
//
// Kenshi RE: stealth_mult in race_def scales base factor; crouching ×0.5; sprint ×1.5.
// StealthSystem::Tick() (game/src/ai/) updates stealth_factor each logic tick.

struct StealthComponent {
    float   stealth_factor  = 1.f;   // [0..1]; multiplied into sense activation fill rate
    uint8_t is_crouching    = 0;     // 1 = NPC is crouching (halves stealth_factor)
    uint8_t stealth_mode    = 0;     // 1 = intentional stealth (AI-requested)
    uint8_t stealth_level   = 0;     // [0..100] skill level (from StatSheet::Stealth)
    uint8_t _pad            = 0;
};
static_assert(sizeof(StealthComponent) == 8, "StealthComponent must be 8 bytes");

// Compute stealth_factor from race base, crouch, sprint, and skill.
// race_mult: from RaceDef::stealth_mult (1.0=normal); sprint_active: moving >3 m/s.
inline float CalcStealthFactor(float race_mult, bool crouching,
                                bool sprint_active, uint8_t skill) noexcept {
    float f = race_mult;
    if (crouching)     f *= 0.5f;
    if (sprint_active) f *= 1.5f;
    // Skill bonus: every 10 skill points = -5% detection (max -40% at skill 80)
    float skill_mult = 1.0f - (float)(skill > 80 ? 80 : skill) * 0.005f;
    f *= skill_mult;
    return f < 0.f ? 0.f : f > 1.f ? 1.f : f;
}
