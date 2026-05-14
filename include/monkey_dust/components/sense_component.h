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
// Batch 3: expanded from 2 → 9 senses (MD SENSE_SETS analysis).
// JSON "sense" field accepts name string or integer index (0-8).
enum class SenseType : uint8_t {
    Visual     = 0,  // line-of-sight cone
    Audio      = 1,  // sound stimulus
    Smell      = 2,  // chemical/scent trail
    Vibration  = 3,  // footstep tremors
    Touch      = 4,  // physical contact
    Peripheral = 5,  // edge-of-vision movement
    Motion     = 6,  // motion tracker ping
    Anxiety    = 7,  // psychological stress state
    Background = 8,  // ambient awareness
    COUNT      = 9,
};
static constexpr uint8_t MAX_SENSES = static_cast<uint8_t>(SenseType::COUNT);

// ── SenseComponent ───────────────────────────────────────────────────────────
// M19 component. References a ViewConeSet (loaded into SenseRegistry by index).
// activation[i] ∈ [0..1] per SenseType; threshold_lo/hi shared across all senses.
// last_activated_ms[i]: timestamp (ms) when activation[i] last crossed threshold_hi.
struct SenseComponent {
    uint8_t  cone_set_idx;                  // index into SenseRegistry::sets[]
    uint8_t  _pad[3];
    float    activation[MAX_SENSES];        // [0]=Visual … [8]=Background
    float    threshold_lo;                  // activation below → Unaware
    float    threshold_hi;                  // activation above → Full alert
    float    last_known_x;
    float    last_known_z;
    uint32_t last_activated_ms[MAX_SENSES]; // timestamp when activation[i] last crossed threshold_hi
};
static_assert(sizeof(SenseComponent) == 92, "SenseComponent must be 92 bytes");
