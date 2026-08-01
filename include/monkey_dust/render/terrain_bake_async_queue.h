#pragma once
#include <monkey_dust/render/terrain_bake.h>
#include <atomic>
#include <cstdint>

// TERRAIN_CA_REBUILD_PROMPT.md Phase 3 -- lock-free SPSC ring buffers for
// background terrain baking. Same pattern as engine/include/monkey_dust/
// nav/nav_async_queue.h (single producer = main thread, single consumer =
// worker thread, wait-free both directions) -- mirrored deliberately, not
// reinvented, since that queue already solves the exact same class of
// problem (expensive CPU rebuild work that must not stall the main
// thread) for NavMesh rebuilds.
//
// Why this exists: Phase 0/2's real measurement (terrain_research/perf/
// PROGRESS.md) found TerrainBakedRenderer's synchronous bake-on-cache-
// miss spikes to 100ms on a single frame during heavy camera churn (many
// new patches entering view at once) -- far over the ≤2ms/frame budget.
// Moving the actual TerrainBake_ComputeVertices call off the main thread
// is the fix; GPU upload of a completed result still happens ONLY on the
// main thread (SDL_GPU buffers are not thread-safe to write from a
// worker), per the prompt's own explicit constraint.

// Same worst-case tier-0 vertex count TerrainBake_VertexCount(128) would
// compute -- a compile-time constant here since it sizes a fixed struct
// member, not a runtime-queried value.
static constexpr int TERRAIN_BAKE_MAX_VERTS_PER_REQUEST = 129 * 129 + 4 * 129;

struct TerrainBakeRequest {
    int      tier = -1;
    uint64_t patch_key = 0;
    float    origin_x = 0.f, origin_z = 0.f, patch_size = 0.f;
    bool     has_coarser = false;
    float    normal_step_m = 0.f;
    // Function pointer, not std::function -- trivially copyable across
    // the queue, no allocation. Always TerrainAtlas_SampleWorld in
    // production; matches GetOrBakePatch's existing synchronous
    // signature for consistency (that one also takes this explicitly,
    // for the same testability reason terrain_bake.h's top comment
    // documents).
    TerrainHeightSampleFn sample_height = nullptr;
    bool     valid = false;
};

// Fixed-size vertex array sized for the WORST-CASE tier (tier 0, kPatchN
// quads/edge) -- every result uses this same buffer regardless of which
// tier it's for (a coarser tier just fills fewer of the leading entries,
// vertex_count says how many). Avoids a variable-size payload in a
// fixed-slot ring buffer.
struct TerrainBakeResult {
    int      tier = -1;
    uint64_t patch_key = 0;
    int      vertex_count = 0;
    bool     valid = false;
    TerrainBakedVertex verts[TERRAIN_BAKE_MAX_VERTS_PER_REQUEST];
};

class TerrainBakeAsyncQueue {
public:
    static constexpr int CAPACITY = 8;

    bool TryEnqueueRequest(const TerrainBakeRequest& req) {
        int h    = req_head_.load(std::memory_order_relaxed);
        int next = (h + 1) % CAPACITY;
        if (next == req_tail_.load(std::memory_order_acquire)) return false;
        requests_[h] = req;
        req_head_.store(next, std::memory_order_release);
        return true;
    }

    bool TryDequeueRequest(TerrainBakeRequest& out) {
        int t = req_tail_.load(std::memory_order_relaxed);
        if (t == req_head_.load(std::memory_order_acquire)) return false;
        out = requests_[t];
        req_tail_.store((t + 1) % CAPACITY, std::memory_order_release);
        return true;
    }

    // result must already be fully filled (verts[0..vertex_count)) by the
    // worker -- copies the whole fixed-size struct into the ring slot.
    bool TryEnqueueResult(const TerrainBakeResult& result) {
        int h    = res_head_.load(std::memory_order_relaxed);
        int next = (h + 1) % CAPACITY;
        if (next == res_tail_.load(std::memory_order_acquire)) return false;
        results_[h] = result;
        res_head_.store(next, std::memory_order_release);
        return true;
    }

    bool TryDequeueResult(TerrainBakeResult& out) {
        int t = res_tail_.load(std::memory_order_relaxed);
        if (t == res_head_.load(std::memory_order_acquire)) return false;
        out = results_[t];
        res_tail_.store((t + 1) % CAPACITY, std::memory_order_release);
        return true;
    }

private:
    TerrainBakeRequest requests_[CAPACITY];
    std::atomic<int> req_head_{0}, req_tail_{0};

    TerrainBakeResult results_[CAPACITY];
    std::atomic<int> res_head_{0}, res_tail_{0};
};
