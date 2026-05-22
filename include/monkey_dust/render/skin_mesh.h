#pragma once
// SkinMesh — loads a skinned GLB (POSITION+NORMAL+JOINTS_0+WEIGHTS_0+skin+animations).
// Vertex stride = 44: pos(12) + norm(12) + joints_u8x4(4) + weights_f4(16).
// Animation clips are stored CPU-side; GetFinalBones() produces mat4[MAX_SKIN_BONES].

#include <monkey_dust/render/gpu_hal.h>
#include <cstdint>
#include <cstring>
#include <cmath>

static constexpr int MAX_SKIN_BONES = 64;
static constexpr int MAX_SKIN_CLIPS = 16;
static constexpr int MAX_SKIN_KF    = 512; // keyframes per track

struct SkinVertex {
    float    x, y, z;      // position  (offset  0, 12 B)
    float    nx, ny, nz;   // normal    (offset 12, 12 B)
    float    u, v;         // texcoord  (offset 24,  8 B)
    uint8_t  j[4];         // joints    (offset 32,  4 B)
    float    w[4];         // weights   (offset 36, 16 B)
};
static_assert(sizeof(SkinVertex) == 52, "SkinVertex stride");

struct SkinKeyframe {
    float t;
    float tx, ty, tz;
    float qx, qy, qz, qw;
};

struct SkinTrack {
    SkinKeyframe kf[MAX_SKIN_KF];
    int          count = 0;
};

struct SkinClip {
    char      name[48]  = {};
    float     duration  = 0.f;
    SkinTrack tracks[MAX_SKIN_BONES];
    int       bone_count= 0;
};

class SkinMesh {
public:
    bool LoadGLB(const char* path);
    void Shutdown();

    // Compute mat4[MAX_SKIN_BONES] for given clip + time.
    // out_bones must point to at least MAX_SKIN_BONES * 16 floats.
    void GetFinalBones(int clip_idx, float time_s, float* out_bones) const;

    // Same loop, but also outputs model-space bone positions (xyz, 3 floats per bone)
    // and optionally the pre-invbind world matrices (float[MAX_SKIN_BONES * 16]).
    // out_world_mats: required by FootIK::SolveLeg — pass s_ik_worlds[MAX_SKIN_BONES*16].
    void GetFinalBonesFull(int clip_idx, float time_s,
                           float* out_bones,
                           float* out_model_xyz,
                           float* out_world_mats = nullptr) const;

    // Patch one skinning matrix after IK correction:
    //   out_bones[bone_idx*16] = new_world_16 * inv_bind_[bone_idx]
    void PatchBoneIK(int bone_idx, const float* new_world_16,
                     float* out_bones) const;

    int ClipCount()            const { return clip_count_; }
    int ClipIndexByName(const char* name) const;
    const char* ClipName(int i) const { return (i>=0&&i<clip_count_)?clips_[i].name:""; }
    float ClipDuration(int i)  const { return (i>=0&&i<clip_count_)?clips_[i].duration:0.f; }

    // Apply inverse bind matrices: bones[i] = bones[i] * inv_bind_[i].
    // Call after GetFinalBones*() to get correct GPU skinning matrices.
    void ApplyInvBind(float* bones) const;

    // Upper/lower body blend: evaluate base_clip for all bones, then override lower-body
    // bones from override_clip (use lower_mask[i]=true for bones to override).
    // Pass lower_mask=nullptr to skip override (equivalent to plain GetFinalBones).
    // out_world_mats: optional float[MAX_SKIN_BONES*16] — same world mats used internally,
    //   required by FootIK::SolveLeg so IK uses the same matrices as the skinning output.
    void GetFinalBonesBlend(int base_clip, float base_t,
                            int override_clip, float override_t,
                            const bool* lower_mask, float* out,
                            float* out_world_mats = nullptr) const;

    // CPU vertex data — valid until next LoadGLB() call (points to internal static buffer).
    const SkinVertex* CpuVerts()  const { return cpu_verts_; }
    uint32_t          VertCount() const { return vert_count_; }

    GpuStaticBuffer vbo;
    GpuStaticBuffer ibo;
    uint32_t        index_count  = 0;
    uint32_t        vert_count_  = 0;
    bool            loaded       = false;
    bool            indices_u16  = true;
    int             bone_count   = 0;
    char            bone_names[MAX_SKIN_BONES][32] = {};

private:
    const SkinVertex* cpu_verts_ = nullptr;
    // Inverse bind matrices per bone (mat4, column-major)
    float inv_bind_[MAX_SKIN_BONES][16] = {};
    // Parent index per bone (-1 = root)
    int   parent_[MAX_SKIN_BONES]  = {};
    // Bind-pose local TRS per bone
    float bind_t_[MAX_SKIN_BONES][3] = {};
    float bind_q_[MAX_SKIN_BONES][4] = {};
    float bind_s_[MAX_SKIN_BONES][3] = {};
    // Topological processing order (parent always before child)
    int   process_order_[MAX_SKIN_BONES] = {};

    SkinClip clips_[MAX_SKIN_CLIPS];
    int      clip_count_ = 0;

    // Helpers
    static void mat4_identity(float* m);
    static void mat4_from_trs(float* m, const float* t, const float* q, const float* s);
    static void mat4_mul(float* out, const float* a, const float* b);
    static void quat_lerp(float* out, const float* a, const float* b, float t);
    static void lerp3(float* out, const float* a, const float* b, float t);
    static float slerp_t(const SkinTrack& tr, float time_s,
                         float* out_t, float* out_q);
};
