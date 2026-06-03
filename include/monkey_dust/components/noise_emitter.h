#pragma once
#include <monkey_dust/components/agent_state.h>  // NoiseType
#include <cstdint>

// NoiseEmitter — B-3: per-entity noise source with explicit radius.
// SenseSystem reads NoiseEmitter neighbours and fills observer
// SenseComponent::activation[AudioCombat=1] or activation[AudioMovement=2].
// NoiseType reuses the existing enum from agent_state.h.
//
// Kenshi RE: noise_range, NoiseTracking; AI.exe: noise_level 0–255.
// Weapon/Explosion → AudioCombat; Footstep/Vent → AudioMovement.

struct NoiseEmitter {
    float     noise_radius_m = 0.f;   // 0 = silent (disabled)
    uint8_t   noise_type     = (uint8_t)NoiseType::None;
    uint8_t   _pad[3]        = {};
};
static_assert(sizeof(NoiseEmitter) == 8, "NoiseEmitter must be 8 bytes");

// SmellEmitter — B-3: per-entity smell source (blood, food, eggs).
// Kenshi RE: smell_blood, smell_eggs, blood_smell_mult.
// SenseSystem fills SenseComponent::activation[Smell=3] from nearby emitters.

enum class SmellType : uint8_t {
    Blood = 0,
    Eggs  = 1,
    Food  = 2,
};

struct SmellEmitter {
    float     smell_radius_m  = 0.f;  // 0 = no smell (disabled)
    uint8_t   smell_type      = 0;    // SmellType
    uint8_t   intensity       = 128;  // [0..255] → [0..1] activation contribution
    uint8_t   _pad[2]         = {};
};
static_assert(sizeof(SmellEmitter) == 8, "SmellEmitter must be 8 bytes");
