#pragma once
#include <cstdint>

// ── ViewCone ─────────────────────────────────────────────────────────────────
// Single perception cone. AI VIEW_CONE_SETS/*.xml analog.
// dist/exposure/movement/stance pairs = [lower, upper] activation modifiers.
struct ViewCone {
    float h_angle_deg, v_angle_deg;  // field of view
    float length_m;                  // max detection range
    float dist_lo,     dist_hi;      // distance effect on activation
    float exposure_lo, exposure_hi;  // light_meter effect
    float movement_lo, movement_hi;  // target movement effect
    float stance_lo,   stance_hi;    // crouch vs stand penalty
};

// ── SenseType ─────────────────────────────────────────────────────────────────
// B-4: Audio split — RE-confirmed (AI.exe FUN_006ca0f0): HEARD_COMBAT=1,
// HEARD_MOVEMENT=2 are separate entries with different activation thresholds.
// Total: 10 sense types (was 9). sizeof(SenseComponent) updated to 112.
// JSON "sense" field accepts name string or integer index (0-9).
enum class SenseType : uint8_t {
    Visual        = 0,  // line-of-sight cone
    AudioCombat   = 1,  // gunshot, clash — high urgency (RE: HEARD_COMBAT)
    AudioMovement = 2,  // footstep, rustle — lower urgency (RE: HEARD_MOVEMENT)
    Smell         = 3,  // chemical/scent trail
    Vibration     = 4,  // footstep tremors
    Touch         = 5,  // physical contact
    Peripheral    = 6,  // edge-of-vision movement
    Motion        = 7,  // motion tracker ping
    Anxiety       = 8,  // psychological stress state
    Background    = 9,  // ambient awareness
    COUNT         = 10,
};
static constexpr uint8_t MAX_SENSES = static_cast<uint8_t>(SenseType::COUNT);

// ── Sensory activation thresholds (RE-confirmed, AI.exe FUN_006c7160) ────────
// 4-level system confirmed across AI.exe ("TRACE/LOWER/ACTIVATED/UPPER_THRESHOLD").
// These are the activation [0..1] breakpoints, not menace levels.
static constexpr float SENSE_LOWER_THRESHOLD     = 0.25f; // below → ignore stimulus
static constexpr float SENSE_ACTIVATED_THRESHOLD = 0.55f; // investigate / alert
static constexpr float SENSE_UPPER_THRESHOLD     = 0.85f; // immediate attack response
static constexpr float SENSE_FACING_TOLERANCE_DEG= 30.f;  // visual cone angular slack

// ── SenseModifiers ────────────────────────────────────────────────────────────
// AI-2: per-entity multipliers that scale activation thresholds (Kenshi/AI RE, 40B).
// Attach as a separate component for NPCs that differ from base SenseComponent defaults.
// All fields are multiplicative (1.0 = no change).
struct SenseModifiers {
    float visual_range_mult    = 1.0f;  // scales cone length_m
    float audio_range_mult     = 1.0f;  // scales audio detection radius
    float activation_fill_mult = 1.0f;  // scales activation fill rate (all senses)
    float activation_drain_mult= 1.0f;  // scales activation drain rate
    float darkness_mult        = 1.0f;  // penalty when low light (exposure)
    float fog_mult             = 1.0f;  // weather visibility penalty
    float crouch_mult          = 1.0f;  // reduction when target is crouching
    float sprint_mult          = 1.0f;  // boost when target is sprinting
    float noise_mult           = 1.0f;  // scales sound stimulus amplitude
    float smell_mult           = 1.0f;  // scales scent radius
};
static_assert(sizeof(SenseModifiers) == 40, "SenseModifiers must be 40 bytes");

// ── VisualSenseEffects ────────────────────────────────────────────────────────
// AI-3: instantaneous visual effect overrides applied to the SenseComponent
// during special events (e.g. flashbang, darkness grenade, eye damage).
// All fields are additive deltas on the SenseComponent activation for one tick.
// sizeof = 16 bytes (4 floats).
struct VisualSenseEffects {
    float blind_duration_s  = 0.f;  // seconds of full visual blackout
    float dazzle_mult       = 0.f;  // activation multiplier during dazzle (0=blind)
    float range_override_m  = 0.f;  // hard cap on visual range_m (0=no override)
    float fade_rate         = 0.f;  // how fast effects decay per second
};
static_assert(sizeof(VisualSenseEffects) == 16, "VisualSenseEffects must be 16 bytes");

// ── SenseComponent ───────────────────────────────────────────────────────────
// M19 component. References a ViewConeSet (loaded into SenseRegistry by index).
// activation[i] ∈ [0..1] per SenseType; threshold_lo/hi shared across all senses.
// last_activated_ms[i]: timestamp (ms) when activation[i] last crossed threshold_hi.
//
// CATHODE RE §7: NPC_AllSensesLimiter + NPC_SenseLimiter — two-tier budget.
// sense_cooldown_frames: per-NPC throttle set by SenseSystem when global budget
// is exceeded. While > 0, sense queries are skipped and decremented each tick.
// 0 = no throttle (normal operation). Max ~30 = 3s at 10 TPS.
// alignas(16): activation[10] array starts at offset 4 — load 4 activations via _mm_load_ps.
// Layout: 4(header) + 40(activation[10]) + 16(thresholds+pos) + 40(last_ms[10]) = 100 → pad → 112.
struct alignas(16) SenseComponent {
    uint8_t  cone_set_idx;                  // index into SenseRegistry::sets[]
    uint8_t  sense_cooldown_frames;         // CATHODE: per-NPC sense budget throttle
    uint8_t  _pad[2];
    float    activation[MAX_SENSES];        // [0]=Visual … [9]=Background
    float    threshold_lo;                  // activation below → Unaware
    float    threshold_hi;                  // activation above → Full alert
    float    last_known_x;
    float    last_known_z;
    uint32_t last_activated_ms[MAX_SENSES]; // timestamp when activation[i] last crossed threshold_hi
    // Implicit 12-byte trailing pad from alignas(16): 100 raw bytes → 112 total.
};
static_assert(sizeof(SenseComponent) == 112, "SenseComponent must be 112 bytes (B-4: COUNT 9→10, alignas(16) pads 100→112)");
