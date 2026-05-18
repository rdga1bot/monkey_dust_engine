#pragma once
// OzzAnimPlayer — runtime animation evaluator using ozz-animation.
// Replaces SkinMesh::GetFinalBones() with proper blend trees and IK.
//
// Usage:
//   OzzAnimPlayer::Get().Load("game/data/anim/kenshi_human.ozz");
//   OzzAnimPlayer::Get().LoadClip("game/data/anim/idle_stand_normal.ozz");
//   // Per logic tick per NPC:
//   OzzAnimPlayer::Get().Eval(slot, clip_lo, clip_hi, blend_w, time_lo, time_hi, out_bones);

#include <cstdint>

// ozz forward-declares to avoid pulling all headers into engine consumers
namespace ozz {
namespace animation {
class Skeleton;
class Animation;
namespace offline { }
}
namespace math { struct SoaTransform; struct Float4x4; }
}

static constexpr int OZZ_MAX_CLIPS  = 16;
static constexpr int OZZ_MAX_JOINTS = 64;

class OzzAnimPlayer {
public:
    static OzzAnimPlayer& Get() { static OzzAnimPlayer s; return s; }

    bool LoadSkeleton(const char* path);
    int  LoadClip(const char* path);        // returns clip index or -1
    int  ClipCount()    const { return clip_count_; }
    int  JointCount()   const { return joint_count_; }
    bool IsReady()      const { return skeleton_ && clip_count_ > 0; }

    // Single-clip evaluation. out_bones: float[OZZ_MAX_JOINTS * 16] (mat4, col-major).
    void Eval(int clip_idx, float time_s, float* out_bones) const;

    // Two-layer blend: lower-body clip_lo + upper-body clip_hi.
    // blend_w: 0 = full idle (hi), 1 = full walk (lo)
    // joint_split: first joint index of upper body (Biped: spine ~= 10)
    void EvalBlend(int clip_lo, int clip_hi,
                   float blend_w, float time_lo, float time_hi,
                   int joint_split,
                   float* out_bones) const;

    void Shutdown();

private:
    OzzAnimPlayer() = default;

    ozz::animation::Skeleton*   skeleton_     = nullptr;
    ozz::animation::Animation*  clips_[OZZ_MAX_CLIPS] = {};
    int                         clip_count_   = 0;
    int                         joint_count_  = 0;

    // Shared working buffers (sequential evaluation — no per-NPC alloc)
    // Declared as raw bytes to avoid pulling ozz headers into .h
    // Sized for OZZ_MAX_JOINTS joints: ceil(64/4)=16 SoaTransforms * 64B = 1KB each
    alignas(16) mutable uint8_t buf_locals_lo_[16 * 64];   // SoaTransform[16]
    alignas(16) mutable uint8_t buf_locals_hi_[16 * 64];
    alignas(16) mutable uint8_t buf_blended_  [16 * 64];
    alignas(16) mutable uint8_t buf_models_   [OZZ_MAX_JOINTS * 64]; // Float4x4[64]
    mutable uint8_t             ctx_lo_buf_   [4096];  // SamplingJob::Context
    mutable uint8_t             ctx_hi_buf_   [4096];
};
