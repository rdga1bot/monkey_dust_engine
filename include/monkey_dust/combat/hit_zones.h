#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────
// HitZones — сегментовані зони влучання.
//
// Кожна зона має:
//   mult        — множник пошкодження
//   hit_chance  — вірогідність потрапити (0.0–1.0)
//   cripple_hp  — поріг HP зони після якого вона "знята"
//
// Стан зон зберігається у CombatComponent (не тут).
// ─────────────────────────────────────────────────────────

// VBfA RE: 19 bone zones (viking.exe.c case 0-18).
// Logical grouping into 6 damage zones (Head/Torso/Arms/Legs) matches Kenshi LimbHealth.
// Shoulder/WeaponBone map to Torso for damage, but retain separate enum for ragdoll routing.
enum class HitZone : uint8_t {
    Head       = 0,  // 1.5× damage, 10% chance
    Torso      = 1,  // 1.0× damage, 45% chance
    LeftArm    = 2,  // 0.8× damage, 10% chance
    RightArm   = 3,  // 0.8× damage, 10% chance
    LeftLeg    = 4,  // 0.9× damage,  8% chance
    RightLeg   = 5,  // 0.9× damage,  8% chance
    LeftShoulder  = 6,  // 0.85× damage, 4% — VBfA: leftshoulder (Bip01 L Clavicle)
    RightShoulder = 7,  // 0.85× damage, 4% — VBfA: rightshoulder (Bip01 R Clavicle)
    WeaponBone    = 8,  // 0.5×  damage, 1% — VBfA: weapon attachment bones
    COUNT      = 9
};

struct HitZoneInfo {
    float    damage_mult;
    float    hit_chance;    // сума всіх = 1.0
    float    cripple_hp;    // HP зони при якому вона виходить з ладу
};

// Таблиця зон (індекс = HitZone enum) — hit_chance сума = 1.0
constexpr HitZoneInfo ZONE_TABLE[(int)HitZone::COUNT] = {
    { 1.50f, 0.10f, 15.0f },  // Head
    { 1.00f, 0.45f, 40.0f },  // Torso
    { 0.80f, 0.10f, 20.0f },  // LeftArm
    { 0.80f, 0.10f, 20.0f },  // RightArm
    { 0.90f, 0.08f, 25.0f },  // LeftLeg
    { 0.90f, 0.08f, 25.0f },  // RightLeg
    { 0.85f, 0.04f, 18.0f },  // LeftShoulder
    { 0.85f, 0.04f, 18.0f },  // RightShoulder
    { 0.50f, 0.01f, 10.0f },  // WeaponBone
};

// ── Bone → HitZone map (md_human_t.glb joint order, RE-verified) ─────────────
// bone_idx = skin joint index from md_human_t.glb (python GLB probe 2026-05-28).
// VBfA RE bone names → Bip01 mapping confirmed: head=Head, hips=Pelvis, spine*=Spine*,
// leftupleg=L Thigh, leftweaponbone=Prop1, leftshoulder=L Clavicle, etc.
// Unmapped bones (root Bip01, toes, nubs, jaw) resolve to Torso as fallback.
struct BoneZoneEntry {
    uint8_t  bone_idx;
    HitZone  zone;
    char     bone_name[24]; // Bip01 name for debug / JSON override
};

static constexpr int BONE_ZONE_COUNT = 19;
constexpr BoneZoneEntry BONE_ZONE_MAP[BONE_ZONE_COUNT] = {
    {  1, HitZone::Torso,         "Bip01 Pelvis"      },  // VBfA: hips
    {  2, HitZone::LeftLeg,       "Bip01 L Thigh"     },  // VBfA: leftupleg
    {  3, HitZone::LeftLeg,       "Bip01 L Calf"      },
    {  4, HitZone::LeftLeg,       "Bip01 L Foot"      },  // VBfA: leftfoot
    {  7, HitZone::RightLeg,      "Bip01 R Thigh"     },  // VBfA: rightupleg
    {  8, HitZone::RightLeg,      "Bip01 R Calf"      },
    {  9, HitZone::RightLeg,      "Bip01 R Foot"      },  // VBfA: rightfoot
    { 12, HitZone::Torso,         "Bip01 Spine"       },  // VBfA: spine
    { 13, HitZone::Torso,         "Bip01 Spine1"      },  // VBfA: spine1/spine2
    { 14, HitZone::Torso,         "Bip01 Spine2"      },  // VBfA: spine3
    { 15, HitZone::LeftShoulder,  "Bip01 L Clavicle"  },  // VBfA: leftshoulder
    { 16, HitZone::LeftArm,       "Bip01 L UpperArm"  },  // VBfA: leftarm
    { 17, HitZone::LeftArm,       "Bip01 L Forearm"   },
    { 18, HitZone::LeftArm,       "Bip01 L Hand"      },  // VBfA: lefthand
    { 19, HitZone::WeaponBone,    "Bip01 Prop1"       },  // VBfA: leftweaponbone
    { 20, HitZone::Head,          "Bip01 Neck"        },
    { 21, HitZone::Head,          "Bip01 Head"        },  // VBfA: head
    { 25, HitZone::RightShoulder, "Bip01 R Clavicle"  },  // VBfA: rightshoulder
    { 26, HitZone::RightArm,      "Bip01 R UpperArm"  },  // VBfA: rightarm
};

// Map a bone index to its HitZone; returns Torso for unmapped bones.
inline HitZone BoneToHitZone(uint8_t bone_idx) {
    for (int i = 0; i < BONE_ZONE_COUNT; ++i)
        if (BONE_ZONE_MAP[i].bone_idx == bone_idx) return BONE_ZONE_MAP[i].zone;
    return HitZone::Torso;
}

// Вибрати зону за псевдорандомом (зважена вибірка за hit_chance)
inline HitZone RollHitZone(unsigned int& rng_state) {
    rng_state = rng_state * 1664525u + 1013904223u;
    float roll = (float)(rng_state >> 8) / (float)(1 << 24);  // 0.0–1.0

    float cumulative = 0.0f;
    for (int i = 0; i < (int)HitZone::COUNT; ++i) {
        cumulative += ZONE_TABLE[i].hit_chance;
        if (roll < cumulative)
            return (HitZone)i;
    }
    return HitZone::Torso;
}
