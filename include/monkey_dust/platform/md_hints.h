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
