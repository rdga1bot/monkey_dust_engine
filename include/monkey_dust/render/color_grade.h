#pragma once
#include <cstdint>
#include <cstring>

// ColorGradeUBO — polynomial tone curve + gamma/saturation.
// VBfA MD_VBfA_IMPL.md §VBfA-R: std140 80B UBO.
//
// Curve: each channel has 4 cubic coefficients [a, b, c, d]:
//   out = a*in^3 + b*in^2 + c*in + d    (where 'in' ∈ [0, 1])
// Neutral: {0, 0, 1, 0} for each channel (identity).
//
// Layout (std140, 80B):
//   eq[0..2]: vec4 per channel (R, G, B) — 3×16 = 48B
//   gamma:    float — 4B
//   saturation: float — 4B
//   brightness: float — 4B
//   contrast:   float — 4B
//   _pad:       16B   → total 80B
//
// Usage:
//   ColorGradeUBO ubo;
//   ubo.SetNeutral();
//   // Upload to GPU as push-constant or UBO binding.

struct alignas(16) ColorGradeUBO {
    float eq[3][4] = {            // cubic curve per channel (R=0, G=1, B=2)
        {0.f, 0.f, 1.f, 0.f},    // R: identity
        {0.f, 0.f, 1.f, 0.f},    // G: identity
        {0.f, 0.f, 1.f, 0.f},    // B: identity
    };
    float gamma      = 2.2f;      // display gamma (sRGB = 2.2)
    float saturation = 1.0f;      // 0=greyscale, 1=neutral, 2=vivid
    float brightness = 0.0f;      // additive brightness offset
    float contrast   = 1.0f;      // multiplicative contrast
    float _pad[4]    = {};        // align to 80B std140 block
};
static_assert(sizeof(ColorGradeUBO) == 80, "ColorGradeUBO must be 80 bytes (std140)");

// Preset: neutral identity (no tone mapping).
inline void SetNeutral(ColorGradeUBO& ubo) noexcept {
    for (int ch = 0; ch < 3; ++ch) {
        ubo.eq[ch][0] = 0.f;  // a (cubic)
        ubo.eq[ch][1] = 0.f;  // b (quadratic)
        ubo.eq[ch][2] = 1.f;  // c (linear)
        ubo.eq[ch][3] = 0.f;  // d (constant)
    }
    ubo.gamma = 2.2f; ubo.saturation = 1.f;
    ubo.brightness = 0.f; ubo.contrast = 1.f;
}

// Preset: warm (sunrise) tint — slight orange lift.
inline void SetWarm(ColorGradeUBO& ubo) noexcept {
    SetNeutral(ubo);
    ubo.eq[0][2] = 1.05f;  // R: slight lift
    ubo.eq[2][2] = 0.92f;  // B: slight reduce
    ubo.saturation = 1.1f;
}

// Preset: desaturated (dusk/dark) — Kenshi's signature look.
inline void SetDesaturated(ColorGradeUBO& ubo) noexcept {
    SetNeutral(ubo);
    ubo.saturation = 0.7f;
    ubo.contrast   = 1.1f;
}

// Global accessor (singleton, set once at startup or per-biome).
inline ColorGradeUBO& GlobalColorGrade() noexcept {
    static ColorGradeUBO ubo;
    static bool init = false;
    if (!init) { SetNeutral(ubo); init = true; }
    return ubo;
}
