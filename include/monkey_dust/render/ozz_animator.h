#pragma once

// SkinMesh is forward-declared here to avoid a circular include.
// ozz_animator.cpp includes skin_mesh.h directly (no cycle in .cpp files).
class SkinMesh;

// Local constant — must equal OZZ_ANIM_MAX_BONES in skin_mesh.h.
// Enforced by static_assert in ozz_animator.cpp.
static constexpr int OZZ_ANIM_MAX_BONES = 64;

#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/memory/unique_ptr.h>

// OzzAnimator — production animation sampling backend.
//
// Replaces SkinMesh::GetFinalBonesFull / GetFinalBonesBlend with ozz's
// SIMD-optimised SamplingJob + BlendingJob + LocalToModelJob.
//
// Thread-safe: Sample/Blend/BlendAdditive use thread_local scratch so
// multiple async T2 jobs can call concurrently without races.
//
// Output: float[OZZ_ANIM_MAX_BONES * 16] column-major mat4 per bone,
//         identical layout to the old CPU LBS output → UploadBonesInCmd unchanged.

class OzzAnimator {
public:
    // Build ozz Skeleton + Animations from a loaded SkinMesh.
    // Called once from NpcRender::Init() after mesh.LoadGLB().
    bool Init(const SkinMesh& mesh);
    void Shutdown();

    bool IsLoaded() const { return loaded_; }
    int  ClipCount()           const { return (int)anims_.size(); }
    float ClipDuration(int idx) const;

    // ── Sampling API (mirrors old GetFinalBones* API) ─────────────────────────

    // Single clip → out_bones[OZZ_ANIM_MAX_BONES * 16]
    // out_model_xyz: optional float[OZZ_ANIM_MAX_BONES * 3] model-space positions (foot IK)
    // bone_scales:   optional float[OZZ_ANIM_MAX_BONES][3] per-bone {sx,sy,sz}
    //   skinning_mat[b] = world[b] * diag(sx,sy,sz) * inv_bind[b]
    //   Matches editor preview's UpdateBoneScales() formula for body development.
    void Sample(int clip_idx, float time_s,
                float* out_bones,
                float* out_model_xyz = nullptr,
                const float (*bone_scales)[3] = nullptr) const;

    // Upper/lower body split:
    //   lower_mask[b]=true  → bone b driven by over_clip (lower body walk)
    //   lower_mask[b]=false → bone b driven by base_clip (upper body walk / full)
    void Blend(int base_clip, float base_t,
               int over_clip,  float over_t,
               const bool* lower_mask, float* out_bones) const;

    // Output raw world matrices per bone (before inv_bind multiply).
    // Used by editor preview: applies its own scale formula (s_posScale + s_boneScales).
    // out_world: float[OZZ_ANIM_MAX_BONES][16], column-major.
    void SampleWorldMats(int clip_idx, float time_s,
                         float (*out_world)[16]) const;

    // Additive layer ON TOP of already-computed base_bones.
    // out_bones modified in-place: base + (add_pose - ref_pose) * weight
    void BlendAdditive(float* out_bones,
                       int add_clip, float add_t,
                       float weight) const;

private:
    // ozz runtime (built once at Init)
    ozz::unique_ptr<ozz::animation::Skeleton>                     skel_;
    ozz::vector<ozz::unique_ptr<ozz::animation::Animation>>       anims_;

    // Inv-bind matrices for skinning: skinning_mat = world * inv_bind
    float inv_bind_[OZZ_ANIM_MAX_BONES][16];

    // Joint index mapping (built by matching bone_names[] to ozz joint_names())
    int bone_to_ozz_[OZZ_ANIM_MAX_BONES];  // our bone idx → ozz joint idx
    int ozz_to_bone_[OZZ_ANIM_MAX_BONES];  // ozz joint idx → our bone idx
    int bone_count_ = 0;

    // Precomputed SoA weight arrays for lower/upper body blend
    // One SimdFloat4 per SoA slot (4 joints per slot); size = num_soa_joints
    ozz::vector<ozz::math::SimdFloat4> upper_weights_soa_;  // base layer (upper body)
    ozz::vector<ozz::math::SimdFloat4> lower_weights_soa_;  // override layer (lower body)

    bool loaded_ = false;

    void ModelsToOutBones(const ozz::span<const ozz::math::Float4x4> models,
                          float* out_bones,
                          float* out_model_xyz,
                          const float (*bone_scales)[3] = nullptr) const;
};
