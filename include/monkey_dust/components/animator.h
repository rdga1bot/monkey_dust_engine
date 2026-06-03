#pragma once
#include <stdint.h>

// AnimatorComponent — data contract between gameplay and rendering.
//
// AnimatorSystem (logic_tick, 10 TPS) writes: clip_*, tgt_blend, anim_speed.
// NpcRender (render frame, 60 FPS) reads clip_* and writes: blend_t, phase.
// cull.comp reads: lod_tier (written by NpcRender).
//
// Step 2: clip_overlay + overlay_weight for additive layers (clothing/combat).
// Step 3: GPU skinning.comp replaces CPU LBS; reads this component directly.

struct AnimatorComponent {
    // ── Gameplay → Render (set by AnimatorSystem @ 10 TPS) ──────────────────
    // All clip indices default to -1 (unset). Render falls back to sm_idle etc.
    int8_t  clip_lo        = -1;  // lower-body walk/jog clip index (-1 = no walk)
    int8_t  clip_hi        = -1;  // upper-body walk/jog clip index (-1 = no walk)
    int8_t  clip_idle      = -1;  // idle clip — render falls back to sm_idle if -1
    int8_t  clip_breathing = -1;  // breathing overlay (-1 = none)
    int8_t  clip_overlay   = -1;  // additive overlay clip (-1 = none)  [Step 2]
    float   tgt_blend      = 0.f; // target blend: 0.0=idle … 1.0=walk/jog
    float   anim_speed     = 1.f; // phase advance multiplier (1.0=walk, 1.7=jog)
    float   overlay_weight = 0.f; // additive blend weight [Step 2]

    // ── Render internal state (updated per render-frame @ 60 FPS) ────────────
    float   blend_t;         // current smooth blend (lerps toward tgt_blend)
    float   phase;           // walk/jog phase accumulator (advances while moving)

    // ── Shared (render writes, cull.comp reads via AnimationSoA) ─────────────
    uint8_t lod_tier;        // 0=T0 full … 4=T4 hidden (set by render)

    // ── B-9: AnimEvent tracking (logic side, 10 TPS) ─────────────────────────
    float    prev_clip_time_ = 0.f;        // time_s at previous logic tick
    uint32_t prev_clip_id_  = 0xFFFFFFFFu; // clip_id at previous tick (reset on change)
    uint8_t  weapon_in_hand_ = 0;          // 1 after WeaponGrab event, 0 after WeaponRelease
    uint8_t  _pad_anim[3]   = {};
};
