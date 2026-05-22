#pragma once
#include <cstdint>

// LimbHealth — per-limb HP pool (6 limbs, aligned with HitZone enum).
// Limb indices: 0=Head, 1=Torso, 2=LeftArm, 3=RightArm, 4=LeftLeg, 5=RightLeg
// "If any of your limbs fall below 0, you won't be able to fight or walk properly." (Kenshi)

static constexpr int LIMB_COUNT = 6;

// Limb state flags (per-limb bitmask, 1 bit each).
namespace LimbFlag {
    constexpr uint8_t SEVERED    = 1u << 0;  // limb cut off — permanent, no regen
    constexpr uint8_t PROSTHETIC = 1u << 1;  // replaced with prosthetic limb
    constexpr uint8_t CRIPPLED   = 1u << 2;  // hp below cripple threshold
}

struct LimbHealth {
    float   hp  [LIMB_COUNT] = {};
    float   max [LIMB_COUNT] = {};
    uint8_t flags[LIMB_COUNT]= {};  // F4: LimbFlag bitmask per limb
    bool    incapacitated    = false;
    uint8_t _pad[1]          = {};

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

    // F4: Limb severance — permanently removes the limb (hp=max=0, SEVERED flag).
    void Sever(int limb_idx) noexcept {
        if (limb_idx < 0 || limb_idx >= LIMB_COUNT) return;
        hp [limb_idx] = 0.f;
        max[limb_idx] = 0.f;
        flags[limb_idx] |= LimbFlag::SEVERED;
        flags[limb_idx] &= (uint8_t)~LimbFlag::PROSTHETIC;
        incapacitated = true;
    }

    // F4: Attach prosthetic — restores function with reduced max HP.
    void AttachProsthetic(int limb_idx, float prosthetic_max) noexcept {
        if (limb_idx < 0 || limb_idx >= LIMB_COUNT) return;
        if (!(flags[limb_idx] & LimbFlag::SEVERED)) return; // can only attach to severed limb
        max[limb_idx] = prosthetic_max;
        hp [limb_idx] = prosthetic_max * 0.8f;  // start at 80% health
        flags[limb_idx] |= LimbFlag::PROSTHETIC;
        UpdateIncap();
    }

    bool IsSevered   (int i) const noexcept { return (flags[i] & LimbFlag::SEVERED)    != 0; }
    bool HasProsthetic(int i) const noexcept { return (flags[i] & LimbFlag::PROSTHETIC) != 0; }
};

using Health = LimbHealth;
