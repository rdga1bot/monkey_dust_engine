#pragma once
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <monkey_dust/world/terrain_chunk.h>
#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/world/clutter_gen.h>
#include <monkey_dust/world/world_registry.h>
#include <monkey_dust/physics/jolt_world.h>

// ── TerrainStreamQueue ────────────────────────────────────────────────────────
// Single-producer / single-consumer async queue for terrain chunk generation.
//
// Worker thread:  TerrainGen_Build() → copies staged data → ready = true
// Main thread:    TerrainGen_UploadFrom() + Jolt + PropGen → consumed = true
//
// Per-slot staging copies (TERRAIN_VERTS*52 + TERRAIN_IDX*2 + skirt) ≈ 271 KB.
// CAPACITY=14 (2×TNKN) → ~3.8 MB BSS; well within 4-8 GB system RAM budget.

struct TerrainBuildSlot {
    // Request fields — written by main thread before enqueue
    TerrainChunk*    chunk      = nullptr;
    ChunkCoord       coord      = {};
    TerrainGenParams params     = {};
    JPH::BodyID*     jolt_id    = nullptr;  // pointer into terrain_jolt_ids[phsz][phsx]
    int              atlas_ex   = 0;        // zone_origin_x + chunk coord.x for PropGen biome
    int              atlas_ez   = 0;        // zone_origin_z + chunk coord.z

    // Per-slot copies of staging buffers — written by worker after Build
    TerrainVertex staged_v [TERRAIN_VERTS];
    uint16_t      staged_i [TERRAIN_IDX];
    TerrainVertex staged_sv[TERRAIN_SKIRT_VERTS];
    uint16_t      staged_si[TERRAIN_SKIRT_IDX];

    // KEN-CLUTTER Tier 2: per-slot clutter staging (variable count, unlike the
    // fixed-size terrain grid above — see clutter_vc/clutter_ic).
    PropVertex clutter_v[CLUTTER_MAX_VERTS];
    uint16_t   clutter_i[CLUTTER_MAX_IDX];
    int        clutter_vc = 0;
    int        clutter_ic = 0;

    std::atomic<bool> ready    {false};
    std::atomic<bool> consumed {true};   // true = slot is free
};

class TerrainStreamQueue {
public:
    static constexpr int CAPACITY = 14;  // 2 × TNKN (max in-flight per shift step)

    TerrainStreamQueue()  = default;
    ~TerrainStreamQueue() { stop(); }

    void start() {
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this] { worker_loop(); });
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable()) worker_.join();
    }

    // Main thread: enqueue a build request. Returns false if all slots busy.
    bool enqueue(TerrainChunk* chunk, ChunkCoord coord, const TerrainGenParams& params,
                 JPH::BodyID* jolt_id, int atlas_ex, int atlas_ez) {
        for (int i = 0; i < CAPACITY; ++i) {
            auto& s = slots_[i];
            bool expect_consumed = true;
            if (s.consumed.compare_exchange_strong(expect_consumed, false,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                s.chunk    = chunk;
                s.coord    = coord;
                s.params   = params;
                s.jolt_id  = jolt_id;
                s.atlas_ex = atlas_ex;
                s.atlas_ez = atlas_ez;
                s.ready.store(false, std::memory_order_release);
                pending_.fetch_add(1, std::memory_order_release);
                return true;
            }
        }
        return false;  // queue full — caller falls back to synchronous Build
    }

    // Main thread: poll completed slots, call GPU upload + caller-provided callback.
    // Returns number of slots uploaded.
    template<typename UploadFn>
    int poll(UploadFn&& fn) {
        int n = 0;
        for (int i = 0; i < CAPACITY; ++i) {
            auto& s = slots_[i];
            if (s.consumed.load(std::memory_order_relaxed)) continue;
            if (!s.ready.load(std::memory_order_acquire))   continue;
            if (s.chunk) {
                TerrainGen_UploadFrom(*s.chunk,
                                      s.staged_v, s.staged_i,
                                      s.staged_sv, s.staged_si);
                ClutterGen_UploadFrom(*s.chunk, s.clutter_v, s.clutter_vc,
                                      s.clutter_i, s.clutter_ic);
                fn(s);
            }
            s.consumed.store(true, std::memory_order_release);
            ++n;
        }
        return n;
    }

    static TerrainStreamQueue& Get() { static TerrainStreamQueue inst; return inst; }

    // Autonomy system (md.chunk_stats()) — in-flight terrain build requests.
    int PendingCount() const { return pending_.load(std::memory_order_acquire); }

private:
    void worker_loop() {
        while (running_.load(std::memory_order_acquire)) {
            if (pending_.load(std::memory_order_acquire) == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }
            // Find next pending slot (not consumed, not ready)
            for (int i = 0; i < CAPACITY; ++i) {
                auto& s = slots_[i];
                if (s.consumed.load(std::memory_order_relaxed)) continue;
                if (s.ready.load(std::memory_order_relaxed))    continue;
                if (!s.chunk) { s.consumed.store(true); continue; }

                // Task terrain-patches: this worker thread's own sequential
                // processing ("one chunk per iteration" below) never races
                // against itself, but the MAIN thread's synchronous Build+
                // Upload fallback (HandleTerrainStreaming/HandleFlythroughStreaming,
                // main.cpp — used whenever enqueue() reports the queue full)
                // can run at the same moment as this call, on the SAME shared
                // static staging buffers (terrain_gen.cpp) — hold the lock
                // until the staged data is fully copied into this slot.
                {
                    std::lock_guard<std::mutex> tg_lock(TerrainGen_StagingMutex());
                    TerrainGen_Build(*s.chunk, s.coord, s.params);

                    // Copy staging buffers into per-slot storage before marking ready
                    memcpy(s.staged_v,  TerrainGen_StagedVerts(),        sizeof(s.staged_v));
                    memcpy(s.staged_i,  TerrainGen_StagedIndices(),       sizeof(s.staged_i));
                    memcpy(s.staged_sv, TerrainGen_StagedSkirtVerts(),    sizeof(s.staged_sv));
                    memcpy(s.staged_si, TerrainGen_StagedSkirtIndices(),  sizeof(s.staged_si));
                }

                // KEN-CLUTTER Tier 2: bake dense clutter on this same worker thread
                // (heavy enough now — thousands of merged verts — that it must NOT
                // run on the main thread like the cheap PropGen_Build still does).
                ClutterGen_Build(*s.chunk, WorldRegistry::Get().GetBiomeAt(s.atlas_ex, s.atlas_ez));
                int cvc = ClutterGen_StagedVertCount(), cic = ClutterGen_StagedIndexCount();
                memcpy(s.clutter_v, ClutterGen_StagedVerts(),   sizeof(PropVertex) * (size_t)cvc);
                memcpy(s.clutter_i, ClutterGen_StagedIndices(), sizeof(uint16_t)   * (size_t)cic);
                s.clutter_vc = cvc;
                s.clutter_ic = cic;

                s.ready.store(true, std::memory_order_release);
                pending_.fetch_sub(1, std::memory_order_release);
                break;  // one chunk per iteration — prevents staging buffer races
            }
        }
    }

    TerrainBuildSlot     slots_[CAPACITY];
    std::atomic<int>     pending_{0};
    std::atomic<bool>    running_{false};
    std::thread          worker_;
};
