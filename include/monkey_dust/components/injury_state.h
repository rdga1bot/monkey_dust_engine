#pragma once
// InjuryState — per-limb bleeding, resting heal, and KO recovery.
// Extends BleedComponent (single-limb active wound) with per-limb tracking.
// BleedSystem reads InjuryState when present; falls back to BleedComponent otherwise.
//
// Kenshi RE:
//   blood recovery rate   = HP/s while being treated by a doctor
//   bleeding clot rate    = HP/s natural clotting without treatment
//   resting heal rate mult= multiplier when resting on floor vs in a bed
//   stun recovery rate    = seconds until KO ends naturally

#include <monkey_dust/components/health.h>  // LIMB_COUNT
#include <cstdint>

static constexpr float BLEED_CLOT_RATE        = 0.003f;  // HP/s natural clot
static constexpr float BLOOD_RECOVERY_RATE     = 0.02f;   // HP/s with active treatment
static constexpr float RESTING_HEAL_FLOOR      = 0.5f;    // HP/s on floor
static constexpr float RESTING_HEAL_BED        = 2.0f;    // HP/s in bed
static constexpr float RESTING_HEAL_DOCTOR_BED = 5.0f;    // HP/s in bed + doctor
static constexpr float DOCTOR_HEAL_MULT_PER_LV = 0.02f;  // +2% per field_medic level
static constexpr float KO_RECOVERY_S           = 60.f;    // seconds until KO ends unaided
static constexpr float KO_RECOVERY_DOCTOR_S    = 20.f;    // seconds with a doctor

struct InjuryState {
    float   bleed_rate[LIMB_COUNT];   // HP/s per limb (0=no bleed)   24B
    float   resting_heal_rate;        // HP/s current heal rate         4B
    float   ko_recovery_timer;        // seconds until KO ends; ≤0=ok   4B
    uint8_t splinted[LIMB_COUNT];     // 1 = splint applied to limb     6B
    uint8_t has_doctor;               // 1 = doctor NPC is adjacent      1B
    uint8_t doctor_skill;             // field_medic skill 0..99         1B
    uint8_t _pad[2];                  //                                 2B
};                                    //                         total  42B → padded to 44B
static_assert(sizeof(InjuryState) == 44, "InjuryState size");

// Effective heal multiplier from doctor presence + skill.
inline float InjuryDoctorMult(const InjuryState& inj) noexcept {
    if (!inj.has_doctor) return 1.f;
    float m = 1.f + inj.doctor_skill * DOCTOR_HEAL_MULT_PER_LV;
    return (m > 3.f) ? 3.f : m;
}
