#pragma once
#include <cstdint>

// ── ViewCone ─────────────────────────────────────────────────────────────────
// Single perception cone. CATHODE VIEW_CONE_SETS/*.xml analog.
// dist/exposure/movement/stance pairs = [lower, upper] activation modifiers.
struct ViewCone {
    float h_angle_deg, v_angle_deg;  // field of view
    float length_m;                  // max detection range
    float dist_lo,     dist_hi;      // distance effect on activation
    float exposure_lo, exposure_hi;  // light_meter effect
    float movement_lo, movement_hi;  // target movement effect
    float stance_lo,   stance_hi;    // crouch vs stand penalty
};

// ── SenseComponent ───────────────────────────────────────────────────────────
// M19 component. References a ViewConeSet (loaded into SenseRegistry by index).
// activation[0] = Visual [0..1], activation[1] = Audio [0..1].
// SenseCheck BT node (M21) reads activation[param_a] > param_b threshold.
// last_known_x/z updated whenever activation crosses threshold_hi.
struct SenseComponent {
    uint8_t cone_set_idx;         // index into SenseRegistry::sets[]
    uint8_t _pad[3];
    float   activation[2];        // [0]=Visual  [1]=Audio
    float   threshold_lo;         // activation below → Unaware
    float   threshold_hi;         // activation above → Full alert
    float   last_known_x;
    float   last_known_z;
};
static_assert(sizeof(SenseComponent) == 28, "SenseComponent must be 28 bytes");
