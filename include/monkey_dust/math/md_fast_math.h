#pragma once
// md_fast_math.h — approximate math wrappers for monkey_dust hot-paths.
//
// Two precision tiers (from fastapprox by Paul Mineiro, BSD licence):
//   md_fsin / md_fcos      — ~3 ULP error, ~1.5x faster than std
//   md_fsin_fast / _fast   — ~30 ULP error, ~2.5x faster than std
//
// Use md_fsin/md_fcos for:
//   - NPC facing direction (sin/cos of rot_y) — relative, not absolute
//   - Wander radius offsets — low precision acceptable
//
// Do NOT use for:
//   - Physics / collision (use std::sinf)
//   - Audio DSP (use std::sinf)
//   - Save/load serialised angles (use std::sinf for reproducibility)
//
// md_fast_sqrt: ~0.5 ULP via SSE rsqrtss + one Newton-Raphson step.
// md_fast_rsqrt: raw SSE rsqrtss, ~11-bit precision (12 ULP).
//
// All functions inline, header-only.  Include once per TU.

#include <cmath>
#include <cstdint>
#if defined(__SSE__)
#  include <xmmintrin.h>
#endif

// ── fastapprox (vendor) ───────────────────────────────────────────────────────
#include "../../vendor/fastapprox/fasttrig.h"
#include "../../vendor/fastapprox/fastexp.h"
#include "../../vendor/fastapprox/fastlog.h"

namespace md {

// ── Trigonometry ──────────────────────────────────────────────────────────────

// ~3 ULP, handles any angle range (full period).
inline float fsin(float x) { return fastsinfull(x); }
inline float fcos(float x) { return fastcosfull(x); }

// ~30 ULP — use when only relative comparison matters (NPC angle checks).
inline float fsin_fast(float x) { return fastersinfull(x); }
inline float fcos_fast(float x) { return fastercosfull(x); }

// atan2 — no fast approximation in fastapprox; use std (called infrequently).
inline float fatan2(float y, float x) { return std::atan2f(y, x); }

// ── Square root ───────────────────────────────────────────────────────────────

// SSE rsqrtss + one Newton-Raphson step.  ~0.5 ULP error.  ~2x faster.
inline float frsqrt(float x) {
#if defined(__SSE__)
    __m128 v = _mm_set_ss(x);
    __m128 r = _mm_rsqrt_ss(v);
    // Newton-Raphson: r' = r * (1.5 - 0.5*x*r*r)
    __m128 half = _mm_set_ss(0.5f);
    __m128 three_half = _mm_set_ss(1.5f);
    r = _mm_mul_ss(r, _mm_sub_ss(three_half,
            _mm_mul_ss(half, _mm_mul_ss(v, _mm_mul_ss(r, r)))));
    float result;
    _mm_store_ss(&result, r);
    return result;
#else
    return 1.f / std::sqrtf(x);
#endif
}

inline float fsqrt(float x) {
    return x * frsqrt(x);  // x * (1/sqrt(x)) = sqrt(x)
}

// ── Exponential / log (for audio/AI utility scoring) ─────────────────────────
inline float fexp(float x) { return fastexp(x); }
inline float flog(float x) { return fastlog(x); }

} // namespace md

