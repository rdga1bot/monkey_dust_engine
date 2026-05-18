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

    int ClipCount()            const { return clip_count_; }
    int ClipIndexByName(const char* name) const;
    const char* ClipName(int i) const { return (i>=0&&i<clip_count_)?clips_[i].name:""; }
    float ClipDuration(int i)  const { return (i>=0&&i<clip_count_)?clips_[i].duration:0.f; }

    GpuStaticBuffer vbo;
    GpuStaticBuffer ibo;
    uint32_t        index_count  = 0;
    bool            loaded       = false;
    bool            indices_u16  = true;
    int             bone_count   = 0;

private:
    // Inverse bind matrices per bone (mat4, column-major)
    float inv_bind_[MAX_SKIN_BONES][16] = {};
    // Parent index per bone (-1 = root)
    int   parent_[MAX_SKIN_BONES]  = {};
    // Bind-pose local translation per bone
    float bind_t_[MAX_SKIN_BONES][3] = {};
    // Bind-pose local rotation (quaternion xyzw) per bone
    float bind_q_[MAX_SKIN_BONES][4] = {};

    SkinClip clips_[MAX_SKIN_CLIPS];
    int      clip_count_ = 0;

    // Helpers
    static void mat4_identity(float* m);
    static void mat4_from_tq(float* m, const float* t, const float* q);
    static void mat4_mul(float* out, const float* a, const float* b);
    static void quat_lerp(float* out, const float* a, const float* b, float t);
    static void lerp3(float* out, const float* a, const float* b, float t);
    static float slerp_t(const SkinTrack& tr, float time_s,
                         float* out_t, float* out_q);
};
