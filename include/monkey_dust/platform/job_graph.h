#pragma once
// JobGraph — batch-level dependency validation over JobSystem (task #7).
//
// JobSystem::Submit itself is unchanged — still a flat queue + worker pool,
// parallelizing WITHIN one batch (the existing gather->SubmitxN->Flush()
// pattern, see ai/sense_system.h's eval_sense_job, npc_render.cpp's
// eval_t2). JobGraph sits above that: each per-tick/per-frame batch
// declares which resource tags it reads/writes (same FNV-1a hash
// convention as ComponentReflect/RenderPassGraph), and Run() validates
// that batches are registered in a dependency-safe order — a write must
// be registered before any batch that reads/writes the same resource,
// same violation-detection shape as RenderPassGraph::Validate().
//
// Scope note (read before extending): as of this writing there are only
// two real batches in the whole codebase (sense_system's cone-falloff
// eval, npc_render's T2 animation blend) and they run on different
// cadences (logic tick vs render frame) — they never actually contend
// for the same tick. So Run() executes batches sequentially, in
// registration order, on the calling thread; it does NOT yet dispatch
// non-conflicting batches onto separate OS threads. That's real future
// work, justified once multiple non-conflicting batches actually coexist
// in the same tick (see docs/ARCHITECTURE_PLAN_7_9.md's guidance: add
// more gather->job->scatter candidates first, e.g. TickPhysics's
// integration pass or TickNavigation's waypoint advance, each verified
// safe the same way eval_sense_job/eval_t2 were, before this scheduler
// has anything real to parallelize).
//
// MAX_BATCHES=16, MAX_TAGS=8 per batch — fixed arrays, no heap.

#include <cstdint>

namespace md {

struct JobBatchDesc {
    static constexpr int MAX_TAGS = 8;
    uint32_t reads[MAX_TAGS]  = {};
    uint32_t writes[MAX_TAGS] = {};
    int      read_count  = 0;
    int      write_count = 0;
};

// Runs one batch's own gather->Submit(...)xN->Flush() sequence. `user` is
// whatever context the batch needs (e.g. a GameState*, or nullptr).
using JobBatchFn = void (*)(void* user);

class JobGraph {
public:
    static JobGraph& Get();
    static constexpr int MAX_BATCHES = 16;

    // Register one batch for this Run(). Call once per batch per tick,
    // before Run(). Returns false if MAX_BATCHES exceeded or name empty.
    bool AddBatch(const char* name, const JobBatchDesc& desc, JobBatchFn fn, void* user);

    // Validates registration order (a batch reading/writing resource R
    // must be registered after every batch that writes R — logs a
    // warning if violated, same shape as RenderPassGraph::Validate()),
    // then runs every registered batch's fn() sequentially in
    // registration order. Clears all registered batches when done.
    void Run();

    // True if writes(a) overlaps reads(b)+writes(b), or vice versa.
    static bool Conflicts(const JobBatchDesc& a, const JobBatchDesc& b);

private:
    JobGraph() = default;

    struct Entry {
        char         name[32] = {};
        JobBatchDesc desc;
        JobBatchFn   fn   = nullptr;
        void*        user = nullptr;
    };

    Entry entries_[MAX_BATCHES] = {};
    int   count_ = 0;
};

} // namespace md
