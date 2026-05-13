#pragma once
#include <cstdint>

// ── ProceduralModulator ───────────────────────────────────────────────────────
// CATHODE FLOAT_MODULATE_RANDOM analog: sinusoidal float modulation.
// POD — store directly in NPC data or AgentBlackboard via bb_set_float.
//
// Usage: float val = ProceduralModulator::Eval(mod, now_s);
// phase_offset randomised at spawn via ProceduralModulator::Randomise().
struct ProceduralModulator {
    float base_value;
    float amplitude;
    float frequency;           // Hz
    float frequency_variance;  // per-entity rng offset applied to frequency
    float phase_offset;        // randomised at spawn; seconds
};

namespace md {

// Returns base + amplitude * sin(2π * (freq + freq_var) * now_s + phase).
// freq_variance is treated as a fixed per-entity bias (set at spawn, not re-randomised).
inline float ModulatorEval(const ProceduralModulator& m, float now_s) noexcept {
    // Approximation: avoid trig in hot-path via a fast sine approximation.
    // Uses the Bhaskara I formula: sin(x) ≈ 16x(π-x)/(5π²-4x(π-x)) for x ∈ [0,π].
    // For game purposes the ~0.2% error is imperceptible.
    float freq = m.frequency + m.frequency_variance;
    // Map to [0, 2π] period
    constexpr float TWO_PI = 6.28318530718f;
    constexpr float PI     = 3.14159265359f;
    float angle = freq * TWO_PI * now_s + m.phase_offset;
    // Reduce to [0, 2π]
    angle -= TWO_PI * static_cast<float>(static_cast<int>(angle / TWO_PI));
    if (angle < 0.f) angle += TWO_PI;
    // Map to [0, π] for Bhaskara — handle second half by sign flip
    float sin_val;
    if (angle <= PI) {
        float x = angle;
        float num = 16.f * x * (PI - x);
        float den = 5.f * PI * PI - 4.f * x * (PI - x);
        sin_val = num / den;
    } else {
        float x = angle - PI;
        float num = 16.f * x * (PI - x);
        float den = 5.f * PI * PI - 4.f * x * (PI - x);
        sin_val = -(num / den);
    }
    return m.base_value + m.amplitude * sin_val;
}

// Randomise phase_offset and frequency_variance using a simple LCG seeded by entity id.
// Call once at NPC spawn. seed = (uint32_t)entity or any per-entity unique value.
inline void ModulatorRandomise(ProceduralModulator& m, uint32_t seed) noexcept {
    // LCG: Numerical Recipes constants
    seed = seed * 1664525u + 1013904223u;
    float r0 = static_cast<float>(seed >> 16) / 65535.f; // [0,1]
    seed = seed * 1664525u + 1013904223u;
    float r1 = static_cast<float>(seed >> 16) / 65535.f; // [0,1]

    constexpr float TWO_PI = 6.28318530718f;
    m.phase_offset        = r0 * TWO_PI;
    m.frequency_variance  = (r1 - 0.5f) * m.frequency_variance * 2.f;
}

} // namespace md
