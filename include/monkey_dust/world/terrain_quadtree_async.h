#pragma once
#include <monkey_dust/world/terrain_quadtree.h>
#include <SDL3/SDL_atomic.h>

// Quadtree-LOD terrain rewrite, Phase 4 (see plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md). Wraps
// TerrainQuadtree::SelectVisible in a JobSystem worker task, double-
// buffered so the render thread always has a complete, self-consistent
// result to read without ever blocking on the worker — matches the
// "Naughty Dog fiber pattern" JobSystem's own Submit(fn,data,counter) doc
// comment describes (spin/poll a counter instead of Flush()'s barrier),
// except here the render thread doesn't even spin: it just reads whichever
// buffer is currently marked active, one frame stale at worst.
//
// Thread safety: KickAsync (main/render thread only) refuses to queue a
// second traversal while one is still in flight — SelectVisible over this
// project's node counts (MAX_VISIBLE_NODES=512) runs in low-single-digit
// microseconds, so falling one frame behind under real load is a non-issue,
// while an unbounded queue of stale traversals would be a real one.
// job_args_ is safe to overwrite in KickAsync precisely because that gate
// guarantees the worker isn't still reading it. active_buf_ is an
// SDL_AtomicInt written by the worker (via SDL_SetAtomicInt, after ALL
// writes to that buffer are complete) and read by the render thread (via
// SDL_GetAtomicInt) — SDL3's atomic ops carry the acquire/release fences
// needed for the render thread to never observe a partially-written buffer.
class TerrainQuadtreeAsyncSelector {
public:
    // tree must outlive this selector — ownership stays with the caller
    // (SceneRender in practice), the same tree instance is reused every
    // frame's KickAsync call.
    void Init(const TerrainQuadtree* tree);

    // Call once per frame from the render/main thread. No-op if a previous
    // traversal is still running (see class doc comment). lod_distances
    // must have tree->MaxDepth()+1 entries, capped at kMaxLodEntries.
    void KickAsync(const float cam_pos[3], const float frustum_planes[16],
                   const float* lod_distances);

    // Never blocks. Returns the most recently COMPLETED traversal's
    // result (buffer + count) — safe to read at any time, even while this
    // frame's KickAsync job is still running (it writes the OTHER buffer).
    const TerrainQuadVisibleNode* ActiveVisible(int& out_count);

private:
    static constexpr int kMaxLodEntries = 12; // generous cap; see TerrainQuadtree::Init's max_depth

    struct JobArgs {
        const TerrainQuadtree* tree = nullptr;
        float cam_pos[3]         = {};
        float frustum_planes[16] = {};
        float lod_distances[kMaxLodEntries] = {};
        TerrainQuadVisibleNode* out_buf   = nullptr;
        int*                    out_count = nullptr;
        int                     write_buf_idx = 0;
        SDL_AtomicInt*          active_buf     = nullptr;
    };
    static void s_run_job(void* p);

    const TerrainQuadtree* tree_ = nullptr;

    TerrainQuadVisibleNode buf_[2][TerrainQuadtree::MAX_VISIBLE_NODES];
    int                    buf_count_[2] = {0, 0};
    SDL_AtomicInt          active_buf_   = {};
    SDL_AtomicInt          in_flight_    = {};

    JobArgs job_args_; // owned storage the in-flight job reads from
};
