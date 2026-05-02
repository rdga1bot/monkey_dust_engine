#pragma once
#include <monkey_dust/render/ssbo.h>
#include <monkey_dust/render/gpu_ring_buffer.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/platform/math_types.h>
#include <cstdint>
#include <entt/entt.hpp>

// CPU-side Structure-of-Arrays for NPC transforms.
// GPU upload via:
//   GpuRingBuffer transform_ring_ (binding 0: vec4 xzyr) — per-frame, ring-buffered
//   SSBO          faction_ssbo_   (binding 3: uint faction) — infrequent, single buffer
// Thread-safety: all methods must be called from main thread.
class TransformSoA {
public:
    static constexpr int      MAX_SLOTS     = 65536;
    static constexpr uint32_t INVALID_SLOT  = 0xFFFFFFFFu;
    static constexpr float    DUMMY_POS     = 1e9f;

    static TransformSoA& Get() {
        static TransformSoA inst;
        return inst;
    }

    // SoA position/rotation arrays (16-byte aligned for SSE2)
    alignas(16) float px      [MAX_SLOTS];
    alignas(16) float pz      [MAX_SLOTS];
    alignas(16) float py      [MAX_SLOTS];
    alignas(16) float rot_y   [MAX_SLOTS];
    alignas(16) float dist_sq [MAX_SLOTS]; // filled by BulkComputeDistSq
    uint8_t           faction [MAX_SLOTS];

    entt::entity slot_to_entity[MAX_SLOTS]; // reverse slot → entity
    int          active_count = 0;

    // Ring-buffered transform upload (binding 0); faction stays single-buffer (binding 3).
    GpuRingBuffer transform_ring_;
    SSBO          faction_ssbo_;

    void     Init();
    uint32_t Alloc(entt::entity e, float x, float z, uint8_t faction_id = 0);
    void     Free(entt::entity e);
    void     FlushAoStoSoA(entt::registry& reg);
    // Upload CPU transforms → GPU, bind both SSBOs.
    void     UploadToGPU();
    // Call once per frame AFTER all draw/compute that read transform_ring_.
    void     AdvanceFrame();
    void     BulkComputeDistSq(float cam_x, float cam_z);
    void     BulkComputeLOD(float near_sq, float far_sq, uint8_t* out_lod) const;
    int      BuildMatrices(const uint8_t* lod, float far_sq,
                           Mat4* out_matrices, int max_out) const;
    Mat4     BuildSingleMatrix(uint32_t slot) const;

    // Save v5 accessors (БОРГ-6)
    int      SlotCount() const { return active_count; }
    uint32_t GetSlotForEntity(entt::entity e) const;
    void     AssignSlot(entt::entity e, uint32_t slot, float x, float z, uint8_t faction_id);

private:
    TransformSoA() = default;
};
