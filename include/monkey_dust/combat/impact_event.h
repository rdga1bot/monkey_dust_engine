#pragma once
// ImpactEvent (VBfA-AI6) — discrete hit-reaction knockback component.
//
// Source: Viking Battle for Asgard animation metadata (FRAMES_TO_IMPACT,
// Z_OFFSET_TO_IMPACT, X_OFFSET_TO_IMPACT) reverse-engineered 2026-05-19.
// VBfA did NOT use continuous rigid-body physics for hit reactions —
// every attack stored a pre-computed displacement vector applied over N frames.
// This gives deterministic, cheap reactions for 1000+ NPCs simultaneously.
//
// Lifecycle:
//   1. CombatDispatch: hit lands → emplace_or_replace<ImpactEvent>(target, ...)
//   2. UpdateImpacts() per logic tick: tr.x += dx*vel, tr.z += dz*vel; --frames_left
//   3. When frames_left == 0: ev.active = false (component stays, reused next hit)
//
// Jolt integration:
//   - During active impact, Jolt readback is SUPPRESSED (WorldTransform = ground truth).
//   - Jolt character position is synced TO WorldTransform each tick via SetPosition().
//   - This ensures seamless hand-off back to normal Jolt motion when impact ends.
//
// Usage (no-heap, collect→apply safe — emplace_or_replace is outside view.each):
//   reg.emplace_or_replace<ImpactEvent>(tgt, dx, dz, 0.3f, 6);

#include <cstdint>

struct ImpactEvent {
    float    dx;             //  0  normalised world-space knockback direction X
    float    dz;             //  4  normalised world-space knockback direction Z
    float    knockback_vel;  //  8  displacement per logic tick (metres)
    float    _reserved;      // 12  (future: decay curve / magnitude)
    int      frames_left;    // 16  logic ticks remaining (VBfA: 6 ≈ 0.6s)
    bool     active;         // 20
    uint8_t  _pad[3];        // 21
};
static_assert(sizeof(ImpactEvent) == 24, "ImpactEvent must be 24B");

// Default parameters (matching VBfA FRAMES_TO_IMPACT + impact distances).
namespace ImpactDefaults {
    static constexpr float  kKnockbackVel = 0.3f;  // m/tick = 3 m/s at 10 TPS
    static constexpr int    kFrames       = 6;      // 6 × 100ms = 600ms reaction
}
