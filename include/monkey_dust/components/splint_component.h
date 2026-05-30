#pragma once
#include <monkey_dust/components/health.h>  // LIMB_COUNT
#include <cstdint>

// SplintComponent — per-entity splint/cast applied to broken limbs.
//
// Kenshi RE (kenshi_x64.exe.c): bandage={severity, heal_mult, medic_skill};
// "splint kit" is a separate item from bandage kit — targets bone fractures,
// not soft-tissue wounds. A splint prevents the limb from deteriorating further
// while the NPC rests and lets the bone heal over time.
//
// Usage:
//   Attach when a limb's HP drops below SPLINT_FRACTURE_THRESH.
//   InjurySystem::Tick() reads this: if splinted[limb] && resting → apply heal_rate_mult.
//   Doctor removes splint after limb HP recovers above SPLINT_HEAL_THRESH.

static constexpr float SPLINT_FRACTURE_THRESH = 0.25f;  // limb HP fraction → needs splint
static constexpr float SPLINT_HEAL_THRESH     = 0.70f;  // limb HP fraction → splint removable
static constexpr float SPLINT_HEAL_RATE_MULT  = 1.5f;   // heal speed boost while splinted+resting
static constexpr uint8_t SPLINT_MIN_MEDIC_SKILL = 10;   // FieldMedic skill to apply splint

struct SplintEntry {
    uint8_t wound_severity = 0;   // 0=none, 1=hairline, 2=fracture, 3=shattered
    uint8_t medic_skill    = 0;   // FieldMedic skill of the treating doctor
    float   heal_mult      = 1.f; // effective heal rate multiplier for this limb
};

struct SplintComponent {
    SplintEntry limbs[LIMB_COUNT];  // one entry per limb (6 limbs)
    uint8_t     applied_count = 0;  // how many limbs are currently splinted
    uint8_t     _pad[3]       = {};
};
// sizeof: SplintEntry = {uint8×2, float} = 8B; 6×8 + 4 = 52B
static_assert(sizeof(SplintComponent) == 52, "SplintComponent layout");
