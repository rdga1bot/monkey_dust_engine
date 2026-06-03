#pragma once
#include <monkey_dust/math_types.h>
#include <cstdint>
#include <cmath>

// LookAtIK — C-2: CPU bone constraint for NPC head tracking.
// Applied after SkinMesh::GetFinalBones(), before GPU upload.
// Uses a single bone (head) with yaw/pitch clamped to Kenshi-style limits.

struct LookAtIK {
    float   target_x  = 0.f;  // world-space look target
    float   target_y  = 1.7f; // default: eye height
    float   target_z  = 0.f;
    float   weight    = 0.f;  // [0..1]: 0=disabled, 1=full constraint
};
static_assert(sizeof(LookAtIK) == 16, "LookAtIK must be 16 bytes");

// Maximum head rotation angles (Kenshi-style constraints).
static constexpr float LOOK_MAX_YAW_DEG   = 60.f;
static constexpr float LOOK_MAX_PITCH_DEG = 40.f;

// Apply LookAt constraint to a single bone matrix in-place.
// bones_mat4: flat array of 16 floats per bone (row-major mat4).
// bone_idx: the head bone index (looked up by name at load time).
// origin: world-space position of the bone root (entity world pos + bone offset).
inline void LookAtIK_Apply(float* bones_mat4, int bone_idx,
                             float origin_x, float origin_y, float origin_z,
                             const LookAtIK& ik) noexcept {
    if (ik.weight <= 0.f || bone_idx < 0) return;

    float* m = bones_mat4 + bone_idx * 16;

    // Direction from bone origin to target
    float dx = ik.target_x - origin_x;
    float dy = ik.target_y - origin_y;
    float dz = ik.target_z - origin_z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist < 0.01f) return;

    // Yaw (around Y) and pitch (around X) in degrees
    float yaw_deg   = atan2f(dx, dz) * (180.f / 3.14159265f);
    float pitch_deg = atan2f(-dy, sqrtf(dx*dx + dz*dz)) * (180.f / 3.14159265f);

    // Clamp to joint limits
    if (yaw_deg   >  LOOK_MAX_YAW_DEG)   yaw_deg   =  LOOK_MAX_YAW_DEG;
    if (yaw_deg   < -LOOK_MAX_YAW_DEG)   yaw_deg   = -LOOK_MAX_YAW_DEG;
    if (pitch_deg >  LOOK_MAX_PITCH_DEG) pitch_deg =  LOOK_MAX_PITCH_DEG;
    if (pitch_deg < -LOOK_MAX_PITCH_DEG) pitch_deg = -LOOK_MAX_PITCH_DEG;

    // Scale by weight for smooth blending
    float y_rad = yaw_deg   * ik.weight * (3.14159265f / 180.f);
    float p_rad = pitch_deg * ik.weight * (3.14159265f / 180.f);

    // Build rotation: Ry(yaw) * Rx(pitch) — applied as a delta on the bone matrix
    float cy = cosf(y_rad), sy = sinf(y_rad);
    float cp = cosf(p_rad), sp = sinf(p_rad);

    // Compose delta rotation with existing bone matrix (pre-multiply)
    // Simplified: only modify the rotation columns (upper-left 3×3).
    float r[9] = {
         cy,      0.f,  sy,
         sy*sp,   cp,  -cy*sp,
        -sy*cp,   sp,   cy*cp
    };
    // Apply r to columns [0..2] rows [0..2] of m (row-major mat4)
    float tmp[9];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            tmp[row*3+col] = 0.f;
            for (int k = 0; k < 3; ++k)
                tmp[row*3+col] += r[row*3+k] * m[k*4+col];
        }
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            m[row*4+col] = tmp[row*3+col];
}
