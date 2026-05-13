#pragma once
#include <monkey_dust/platform/md_hints.h>
#include <monkey_dust/render/ssbo.h>
#include <monkey_dust/render/gpu_ring_buffer.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/platform/math_types.h>
#include <cstdint>
#include <entt/entt.hpp>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

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

    // SoA position/rotation arrays.
    // alignas(64) = one cache line; enables AVX2 vmovaps (32-byte aligned load)
    // and prevents false sharing on the first lane. Each array is 65536*4 = 256 KB
    // which is divisible by 64, so subsequent arrays are also 64-byte aligned.
    alignas(64) float px      [MAX_SLOTS];
    alignas(64) float pz      [MAX_SLOTS];
    alignas(64) float py      [MAX_SLOTS];
    alignas(64) float rot_y   [MAX_SLOTS];
    alignas(64) float dist_sq [MAX_SLOTS]; // filled by BulkComputeDistSq
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

#ifdef MD_SDL_GPU
    // Upload xzyr + faction to SDL_GPU device buffers via copy passes in cmd.
    // Call before any SDL_GPU compute pass that reads transform or faction data.
    void UploadSDLGPU(SDL_GPUCommandBuffer* cmd);
    SDL_GPUBuffer* SDLTransformBuffer() const { return sdl_xzyr_buf_; }
    SDL_GPUBuffer* SDLFactionBuffer()   const { return sdl_faction_buf_; }
#endif
    MD_HOT void BulkComputeDistSq(float cam_x, float cam_z);
    MD_HOT void BulkComputeLOD(float near_sq, float far_sq, uint8_t* out_lod) const;
    int      BuildMatrices(const uint8_t* lod, float far_sq,
                           Mat4* out_matrices, int max_out) const;
    Mat4     BuildSingleMatrix(uint32_t slot) const;

    // Save v5 accessors (БОРГ-6)
    int      SlotCount() const { return active_count; }
    uint32_t GetSlotForEntity(entt::entity e) const;
    void     AssignSlot(entt::entity e, uint32_t slot, float x, float z, uint8_t faction_id);

    // Dirty-range tracking for faction SSBO.
    // Faction changes are infrequent; upload only the dirty sub-range.
    void MarkFactionDirty(uint32_t slot) noexcept {
        if (!faction_dirty_) {
            faction_dirty_     = true;
            faction_dirty_min_ = slot;
            faction_dirty_max_ = slot;
            return;
        }
        if (slot < faction_dirty_min_) faction_dirty_min_ = slot;
        if (slot > faction_dirty_max_) faction_dirty_max_ = slot;
    }

private:
    TransformSoA() = default;

    bool     faction_dirty_     = true;  // true on first frame → full upload
    uint32_t faction_dirty_min_ = 0;
    uint32_t faction_dirty_max_ = 0;
#ifdef MD_SDL_GPU
    SDL_GPUBuffer*         sdl_xzyr_buf_    = nullptr;
    SDL_GPUTransferBuffer* sdl_xzyr_stg_    = nullptr;
    SDL_GPUBuffer*         sdl_faction_buf_ = nullptr;
    SDL_GPUTransferBuffer* sdl_faction_stg_ = nullptr;
#endif
};
