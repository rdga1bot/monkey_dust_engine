#pragma once
// AwarenessLimits (VBfA-AI2) — hard caps on NPC perception reactions per tick.
//
// Source: Viking Battle for Asgard global_awareness.bin (XOR-0x08 decoded 2026-05-19).
// VBfA used identical caps to prevent O(n²) alert cascades on large battlefields.
//
// Usage:
//   1. Call AwarenessLimits::g_frame.Reset() at the top of SenseSystemUpdate().
//   2. Before writing last_activated_ms on a rising sense edge, check the counter:
//        if (g_frame.reacted_to_player < MAX_REACT_TO_PLAYER) { ... }
//   3. Increment the matching counter after writing.
//
// Why this works:
//   Without caps: 500 NPCs see player → 500 BT "attack" motivations fire → O(n) spike.
//   With caps: first 200 detect → 300 see but don't react (last_activated_ms unchanged).
//   300 "silent" NPCs perceive visually (activation updated) but BT nodes reading
//   SenseTimeCheck / ConditionAnySenseWithinTime see no recent activation → no reaction.
//   Combined with VBfA-AI1 BT LOD tiers this bounds total AI work per tick.

namespace AwarenessLimits {

// ── Perception ranges (metres) ────────────────────────────────────────────────
static constexpr float MAX_VISION_RANGE       = 22.f;  // hard cut-off for visual detection
static constexpr float MAX_HEARING_RANGE      =  5.f;  // ambient walk/footstep noise
static constexpr float MAX_COMBAT_NOISE_RANGE = 25.f;  // hear a fight with the player
static constexpr float MAX_HORN_RANGE         = 50.f;  // horn alert broadcast radius

// ── Reaction caps (rising edges per tick) ────────────────────────────────────
static constexpr int MAX_REACT_TO_COMBAT = 7;   // max NPCs that hear combat per tick
static constexpr int MAX_REACT_TO_PLAYER = 200; // max NPCs that sight-detect player per tick
static constexpr int MAX_REACT_TO_HORN   = 20;  // max NPCs that respond to horn per tick

// ── Per-tick counters (reset once per SenseSystemUpdate call) ─────────────────
// C++17 inline variable — single definition across all translation units.
struct FrameCounters {
    int reacted_to_combat = 0;
    int reacted_to_player = 0;
    int reacted_to_horn   = 0;
    void Reset() noexcept {
        reacted_to_combat = reacted_to_player = reacted_to_horn = 0;
    }
};
inline FrameCounters g_frame;

} // namespace AwarenessLimits
