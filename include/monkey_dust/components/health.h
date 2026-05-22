#pragma once
#include <cstdint>

// LimbHealth — per-limb HP pool (6 limbs, aligned with HitZone enum).
// Limb indices: 0=Head, 1=Torso, 2=LeftArm, 3=RightArm, 4=LeftLeg, 5=RightLeg
// "If any of your limbs fall below 0, you won't be able to fight or walk properly." (Kenshi)

static constexpr int LIMB_COUNT = 6;

struct LimbHealth {
    float hp [LIMB_COUNT] = {};
    float max[LIMB_COUNT] = {};
    bool  incapacitated   = false;

    // Construct from a single total HP value (all limbs equal share).
    static LimbHealth Make(float total_hp) noexcept {
        LimbHealth h;
        float per = total_hp / LIMB_COUNT;
        for (int i = 0; i < LIMB_COUNT; ++i) h.hp[i] = h.max[i] = per;
        return h;
    }
    // Construct from separate current/max totals.
    static LimbHealth Make(float cur, float maximum) noexcept {
        LimbHealth h;
        float pc = cur / LIMB_COUNT, pm = maximum / LIMB_COUNT;
        for (int i = 0; i < LIMB_COUNT; ++i) { h.hp[i] = pc; h.max[i] = pm; }
        return h;
    }

    float TotalHp()    const noexcept { float s=0; for (auto v:hp)  s+=v; return s; }
    float TotalMax()   const noexcept { float s=0; for (auto v:max) s+=v; return s; }
    float HpFraction() const noexcept { float m=TotalMax(); return m>0.f ? TotalHp()/m : 1.f; }

    // Recompute incapacitated flag (call after any direct hp[] mutation).
    void UpdateIncap() noexcept {
        for (int i = 0; i < LIMB_COUNT; ++i) if (hp[i] <= 0.f) { incapacitated=true; return; }
        incapacitated = false;
    }
};

using Health = LimbHealth;
