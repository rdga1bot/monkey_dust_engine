#pragma once
#include <stdint.h>

// CharBodyState — per-entity body development state.
//
// Stores per-bone scale multipliers ({sx, sy, sz}) that drive body shape:
//   - bone_scales[b][3]: vertex deformation scale (setBoneSize equivalent)
//   - pos_scales[b][3]:  positional scale (setBonePositionalSize equivalent)
//
// Updated by BodyDevelopSystem (10 TPS) based on:
//   - CombatComponent: muscle development from fighting
//   - NeedsComponent:  fat from overeating, wasting from starvation
//   - StatSheet skills: strength, athletics → muscle groups
//
// Read by NpcRender → passed to OzzAnimator::Sample(... bone_scales)
// so in-game appearance matches character editor preview exactly.
//
// Initialized to identity ({1,1,1}) so default NPCs look unchanged.

static constexpr int BODY_MAX_BONES = 64;  // must equal OZZ_ANIM_MAX_BONES

struct CharBodyState {
    float bone_scales[BODY_MAX_BONES][3];  // vertex scale per bone, default {1,1,1}
    float pos_scales [BODY_MAX_BONES][3];  // positional scale, default {1,1,1}

    void Reset() {
        for (int b = 0; b < BODY_MAX_BONES; ++b) {
            bone_scales[b][0] = bone_scales[b][1] = bone_scales[b][2] = 1.f;
            pos_scales [b][0] = pos_scales [b][1] = pos_scales [b][2] = 1.f;
        }
    }
};
