#pragma once
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_bake.h>
#include <monkey_dust/render/terrain_bake_async_queue.h>
#include <monkey_dust/render/terrain_renderer.h>
#include <monkey_dust/render/terrain_patch_renderer.h>
#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <thread>
#include <atomic>

// TERRAIN_CA_REBUILD_PROMPT.md Phase 2 -- GPU-side renderer for the
// BAKED terrain path (--terrain-path=baked). Companion to
// TerrainPatchRenderer (VTF path, unchanged, coexists behind the
// runtime flag) -- draws real per-patch baked vertex buffers (see
// terrain_bake.h) instead of one shared template mesh sampled via a
// heightmap texture. One (non-instanced) draw call per active patch,
// since each patch's vertex buffer holds ITS OWN real heights.
//
// Bounded cache, not "bake the whole world": terrain_research/perf/
// PROGRESS.md's Phase 2 memory-budget entry measured baking ALL 8 tiers
// for all ~9800 world chunks at ~9.28GB -- exceeds the documented
// hardware budget. Baking only the currently-active/visible working
// set (~130-140 patches per Phase 0's baseline) costs 32-96MB.
// kMaxSlotsPerTier below is sized generously against that measured
// working set, not guessed.
class TerrainBakedRenderer {
public:
    static constexpr int kNumTiers        = TerrainPatchRenderer::kNumTiers;   // 8
    static constexpr int kPatchN           = TerrainPatchRenderer::kPatchN;     // 128
    // 32 slots/tier x 8 tiers = 256 total -- Phase 0's baseline observed
    // ~130-140 active patches spread across ~4 active tiers at once;
    // this leaves real headroom without approaching the ~9.28GB
    // all-tiers-all-chunks worst case (256 slots at tier-0's own worst-
    // case per-slot size, ~549KB, is ~137MB total -- still well inside
    // budget even if every slot were pinned to the most expensive tier,
    // which never happens in practice since only ~4 tiers are ever
    // simultaneously active).
    static constexpr int kMaxSlotsPerTier  = 32;

    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    [[nodiscard]] bool IsReady() const { return ready_; }

    // Returns a stable slot index [0, kMaxSlotsPerTier) for `patch_key`
    // (caller-defined stable ID, e.g. a packed (gx,gz) integer) at tier
    // `tier` -- bakes+uploads only if this key isn't already cached in
    // this tier's pool (LRU-evicts the oldest slot when the pool is
    // full). MUST be called OUTSIDE any active render pass (issues a
    // copy pass when a bake is actually needed) and BEFORE the render
    // pass that calls DrawSlot for the returned index. Marks the slot
    // used this frame regardless of whether a bake happened, so
    // eviction only ever picks slots nothing has touched recently.
    int GetOrBakePatch(SDL_GPUCommandBuffer* cmd, int tier, uint64_t patch_key,
                        float origin_x, float origin_z, float patch_size,
                        bool has_coarser, float normal_step_m,
                        TerrainHeightSampleFn sample_height);

    // Draws slot `slot` of tier `tier` as one non-instanced indexed draw
    // call. No-op if GetOrBakePatch was never called for this (tier,slot)
    // this session (slot invalid).
    // world_origin_x/z + world_to_uv: same whole-world colour-overlay
    // convention TerrainPatchRenderer::DrawBatch takes (NOT this patch's
    // own origin -- the overlay texture's own origin/scale, constant
    // across every patch/tier; see npc_render.cpp's kWorldCenter/COL_UV).
    void DrawSlot(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                  int tier, int slot, const float* vp16,
                  float origin_x, float origin_z, float patch_size, float lod,
                  float cam_x, float cam_y, float cam_z,
                  const TerrainRenderer::SunParams& sun,
                  float world_origin_x, float world_origin_z, float world_to_uv,
                  float fog_far, const float fog_color[3], float fog_near,
                  const TerrainRenderer& ground);

    // TERRAIN_CA_REBUILD_PROMPT.md Phase 3 -- async bake path. Real
    // measurement (terrain_research/perf/PROGRESS.md) found the
    // synchronous GetOrBakePatch above spikes to 100ms/frame during
    // heavy camera churn (many new patches at once) -- far over the
    // ≤2ms/frame budget. Same SPSC-worker pattern as engine/include/
    // monkey_dust/nav/nav_async_queue.h's NavSystem (mirrored, not
    // reinvented): a background thread does the actual TerrainBake_
    // ComputeVertices work; GPU upload of a finished result still
    // happens ONLY on the main thread inside PumpAsyncResults (SDL_GPU
    // buffers are not thread-safe to write from a worker).
    //
    // Non-blocking: returns the current slot if this patch is already
    // cached+uploaded, else -1 (not ready yet -- caller should skip
    // drawing this patch THIS frame; it self-heals within a few frames
    // as the worker catches up, the same "pop-in while streaming"
    // tradeoff essentially every background-streaming terrain system
    // makes). Enqueues a bake request if this key isn't already
    // pending (tracked internally) -- safe to call every frame for
    // every visible patch without duplicate-enqueueing.
    int TryGetOrRequestBakeAsync(int tier, uint64_t patch_key,
                                  float origin_x, float origin_z, float patch_size,
                                  bool has_coarser, float normal_step_m,
                                  TerrainHeightSampleFn sample_height);

    // Drains any results the worker thread has finished since the last
    // call and uploads them to their target GPU slot (LRU-picked, same
    // eviction convention as GetOrBakePatch). MUST be called once per
    // frame, OUTSIDE any active render pass, same constraint as
    // GetOrBakePatch/UploadInstances.
    void PumpAsyncResults(SDL_GPUCommandBuffer* cmd);

    // Advance the internal frame counter -- call once per frame, after
    // all this frame's GetOrBakePatch/TryGetOrRequestBakeAsync calls, so
    // the NEXT frame's LRU comparisons see a fresh "used this frame"
    // boundary.
    void EndFrame() { ++frame_counter_; }

    // Read-only per-tier resident-slot count -- same baseline-measurement
    // role as TerrainPatchRenderer::InstanceCount (terrain-perf-rebuild
    // Phase 0).
    [[nodiscard]] int ResidentSlotCount(int tier) const;

    // TERRAIN_CA_REBUILD_PROMPT.md Phase 2 §3 -- depth-only early-Z prepass
    // draw for an already-baked+resident slot (see TerrainPatchRenderer::
    // DrawBatchDepthOnly's doc comment -- same pattern, baked-path variant).
    // No-op if this (tier, slot) isn't resident yet (caller should already
    // be skipping non-ready patches this frame via TryGetOrRequestBakeAsync's
    // -1 return, same as the main color draw).
    void DrawSlotDepthOnly(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                            int tier, int slot, const float* vp16,
                            float origin_x, float origin_z, float patch_size, float lod,
                            float cam_x, float cam_y, float cam_z);

private:
    struct Slot {
        uint64_t key             = 0;
        bool     valid           = false;
        uint64_t last_used_frame = 0;
    };

    GpuPipeline     pipeline_;
    GpuPipeline     prepass_pipeline_;
    GpuStaticBuffer ibo_[kNumTiers];
    uint32_t        idx_count_[kNumTiers] = {};
    GpuVertexBuffer vbo_[kNumTiers][kMaxSlotsPerTier];
    Slot            slots_[kNumTiers][kMaxSlotsPerTier];
    uint64_t        frame_counter_ = 0;
    bool            ready_ = false;

    // Phase 3 async bake worker -- joined (not detached) in Shutdown,
    // BEFORE the GPU device tears down (same destroy-during-upload race
    // this project already hit once for a different loader thread, see
    // CLAUDE.md's Hardware Checklist "ВИРІШЕНО... editor + md.screenshot()
    // heap-corruption крах" entry -- joining here is not optional).
    TerrainBakeAsyncQueue    async_queue_;
    std::thread              worker_;
    std::atomic<bool>        worker_running_{false};

    // Small fixed set of (tier,key) pairs currently enqueued/in-flight,
    // so TryGetOrRequestBakeAsync doesn't spam duplicate requests for a
    // patch that's still being baked. Sized to the queue's own request
    // capacity (can never have more truly in-flight than that) with a
    // little headroom.
    static constexpr int kMaxPending = TerrainBakeAsyncQueue::CAPACITY * 2;
    struct PendingEntry { int tier = -1; uint64_t key = 0; bool used = false; };
    PendingEntry pending_[kMaxPending];

    [[nodiscard]] bool IsPending(int tier, uint64_t key) const;
    void MarkPending(int tier, uint64_t key);
    void ClearPending(int tier, uint64_t key);
};
#endif
