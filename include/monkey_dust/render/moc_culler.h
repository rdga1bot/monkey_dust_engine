#pragma once
// MocCuller — CPU software occlusion culling via Intel MaskedOcclusionCulling.
//
// Workflow (3D orbit camera mode):
//   1. BeginFrame(mvp_matrix[16], vp_w, vp_h) — clear depth buffer, store MVP
//   2. RenderOccluders(verts_xyz, vert_count, tris, tri_count) — render occluder geo
//   3. IsBoxVisible(world_x, world_y, world_z, half_size) → bool — test AABB
//   4. Batched NPC culling: CullNpcs(…) writes visible indices to out_visible[]
//
// Resolution: 320×160 (2 tiles/px, power-of-8×4 as required by MOC).
// AVX2 accelerated on Intel HD 520 (march=native ensures AVX2 compilation).
//
// The MaskedOcclusionCulling object is created lazily and destroyed on Shutdown().
// Thread-safety: NOT thread-safe. Call from main/render thread only.

#include <cstdint>

namespace md {

class MocCuller {
public:
    static MocCuller& Get();

    // Initialise the MOC object. Called once after GpuDevice init.
    // Resolution must be a multiple of 8 (width) × 4 (height).
    bool Init(int res_w = 320, int res_h = 160);
    void Shutdown();

    bool IsReady() const { return moc_ != nullptr; }

    // ── Per-frame API ─────────────────────────────────────────────────────────

    // Clear the depth buffer and store the MVP matrix for this frame.
    // mvp16: column-major 4×4 float (mat4_ptr() result).
    void BeginFrame(const float* mvp16, int vp_w, int vp_h);

    // Render occluder geometry into the depth buffer.
    // verts: float[3] per vertex (x,y,z world space). stride=12.
    // tris:  uint32_t[3] per triangle.
    void RenderOccluders(const float* verts, int vert_count,
                          const uint32_t* tris, int tri_count);

    // Test an axis-aligned box for visibility.
    // Returns true when the box may be visible (conservative — no false negatives).
    // half_size: box half-extents (same value for all axes for a cube).
    bool IsBoxVisible(float cx, float cy, float cz, float half_size) const;

    // Batch-cull NPCs.
    // npc_x[], npc_z[]: NPC world positions (y assumed 0).
    // npc_height: approximate sprite height in world units.
    // out_mask[i] = 1 if NPC i is potentially visible, 0 if culled.
    // Returns number of visible NPCs.
    int CullNpcs(const float* npc_x, const float* npc_z, int count,
                  float npc_height, uint8_t* out_mask) const;

private:
    void*  moc_   = nullptr;  // opaque MaskedOcclusionCulling*
    float  mvp_[16] = {};
    int    vp_w_  = 0;
    int    vp_h_  = 0;
};

} // namespace md
