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
    int8_t  clip_lo;         // lower-body walk/jog clip index (-1 = no walk)
    int8_t  clip_hi;         // upper-body walk/jog clip index (-1 = no walk)
    int8_t  clip_idle;       // idle clip (combat or normal)
    int8_t  clip_breathing;  // breathing overlay (-1 = none)
    int8_t  clip_overlay;    // additive overlay clip (-1 = none)  [Step 2]
    float   tgt_blend;       // target blend: 0.0=idle … 1.0=walk/jog
    float   anim_speed;      // phase advance multiplier (1.0=walk, 1.7=jog)
    float   overlay_weight;  // additive blend weight [Step 2]

    // ── Render internal state (updated per render-frame @ 60 FPS) ────────────
    float   blend_t;         // current smooth blend (lerps toward tgt_blend)
    float   phase;           // walk/jog phase accumulator (advances while moving)

    // ── Shared (render writes, cull.comp reads via AnimationSoA) ─────────────
    uint8_t lod_tier;        // 0=T0 full … 4=T4 hidden (set by render)
};
