#pragma once

// Compiler branch-prediction hints + attribute shortcuts.
// Portable — falls back to no-op on MSVC / unknown compilers.
//
// Usage:
//   if (MD_LIKELY(ptr != nullptr)) { ... }      // branch almost always taken
//   if (MD_UNLIKELY(slot == INVALID)) return;   // guard clause, rarely fires
//   MD_HOT void MySystem::Tick(float dt) { ... }

#if defined(__GNUC__) || defined(__clang__)
#  define MD_LIKELY(x)       __builtin_expect(!!(x), 1)
#  define MD_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#  define MD_FORCE_INLINE    __attribute__((always_inline)) inline
#  define MD_NOINLINE        __attribute__((noinline))
#  define MD_HOT             __attribute__((hot))
#  define MD_COLD            __attribute__((cold))
#else
#  define MD_LIKELY(x)       (x)
#  define MD_UNLIKELY(x)     (x)
#  define MD_FORCE_INLINE    inline
#  define MD_NOINLINE
#  define MD_HOT
#  define MD_COLD
#endif

// ── PERF-14: Fast inverse sqrt / distance helpers ─────────────────────────────
// RE source: kenshi 1,525 + AI.exe 1,339 rsqrtps/rcpps calls (perf_extract_out.md §1).
// All three reference games use rsqrtps instead of sqrtf for normalize/dist ops.
// Accuracy: 1 Newton-Raphson step → ~0.1% relative error (imperceptible at T2+ range).
#ifdef __SSE__
#  include <xmmintrin.h>
MD_FORCE_INLINE float md_rsqrtf(float x) noexcept {
    float r;
    _mm_store_ss(&r, _mm_rsqrt_ss(_mm_load_ss(&x)));
    return r * (1.5f - 0.5f * x * r * r);   // 1 Newton-Raphson refinement
}
#else
MD_FORCE_INLINE float md_rsqrtf(float x) noexcept { return 1.0f / sqrtf(x); }
#endif

// Fast 2D distance  — replaces sqrtf(dx²+dz²) in hot NPC loops.
MD_FORCE_INLINE float md_dist2d(float dx, float dz) noexcept {
    float d2 = dx * dx + dz * dz;
    return d2 > 0.f ? d2 * md_rsqrtf(d2) : 0.f;
}

// Fast 2D normalize — writes unit (ox,oz), returns approx dist.
// Replaces the `dist=sqrtf(...); nx=dx/dist; nz=dz/dist` idiom.
MD_FORCE_INLINE float md_normalize2d(float dx, float dz,
                                     float& ox, float& oz) noexcept {
    float d2  = dx * dx + dz * dz;
    float inv = d2 > 1e-9f ? md_rsqrtf(d2) : 0.f;
    ox = dx * inv;  oz = dz * inv;
    return d2 > 1e-9f ? d2 * inv : 0.f;
}
