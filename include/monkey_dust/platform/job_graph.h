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
// Task #7 completion (logic_tick.cpp): the 5 major per-tick systems
// (TickNeedsAndInjuries, TickAI, TickNavigation, TickCombatAndProjectiles,
// TickPhysics) are now registered through AddBatch()/Run() with read/write
// tags reflecting a full audit of their actual component touch points —
// see logic_tick.cpp's RunLogicTick. That audit found every adjacent pair
// genuinely conflicts (e.g. TickNeedsAndInjuries writes LimbHealth/
// BleedComponent/InjuryState/NpcNeeds/Inventory; TickAI's ai_goal_needs.cpp
// BT actions — actDoctorHeal/actSeekFood/actSeekBed/actSurgery/
// actRepairSkeleton — read AND write the exact same fields cross-entity),
// so Run() below still executes them sequentially in registration order —
// same behavior as before, but now derived from a validated, explicit
// dependency declaration instead of an implicit call sequence in
// RunLogicTick, and future-proofed: any NEW system with a genuinely
// disjoint declared read/write set gets scheduled in parallel for free.
//
// IMPORTANT CAVEAT for whoever adds the next batch: a disjoint declared
// read/write set is NECESSARY but NOT SUFFICIENT for safe co-scheduling.
// JobBatchFn bodies that call MdRegistry::Get()/flecs directly (the norm
// for anything except a pure gather->Submit->scatter batch like
// eval_nav_waypoint_job) must ALSO perform zero structural ECS operations
// (Emplace/Remove/Create/Destroy) anywhere in their call tree while
// running concurrently with another batch — flecs relocates an entity's
// entire archetype row on any structural change (see md_registry.h), so a
// structural op on ANY entity from one thread can invalidate a
// concurrently-held reference on a COMPLETELY UNRELATED entity from
// another thread, even with zero declared tag overlap. This graph's
// Conflicts() has no way to see that hazard — verify it by hand, the same
// way eval_sense_job/eval_t2/eval_nav_waypoint_job were each manually
// confirmed registry-free before being made JobSystem-parallel.
//
// CONFIRMED BY ACTUAL CRASH, 2026-07-11: an earlier attempt at this put
// TickNeedsAndInjuries and TickAI in the same wave with disjoint declared
// tags and no stage-routing at all — SIGSEGV in ecs_iter_fini/
// ecs_query_next within seconds. Root cause (isolated with 3 standalone
// probes, not guessed): flecs query iteration (ecs_query_next) is not
// safe to run concurrently across 2 threads against the same
// flecs::world UNLESS each thread iterates through its own stage
// (world.readonly_begin(true) + get_stage(i)) — critically, the query
// itself must be built ONCE against the main world BEFORE
// readonly_begin() (on-the-fly query construction against a stage still
// crashed in the probes); only the ITERATION (`query.iter(stage).each()`)
// needs to happen inside the readonly+staged window. Separately
// confirmed safe: single-entity get_mut<T>()/try_get<T>() calls on the
// RAW (non-stage) world, concurrent with another thread's staged query
// iteration — so this hazard is specific to query iteration, not every
// MdRegistry call.
//
// FIXED: Run() below now wraps any wave with 2+ batches in
// Registry::Get().readonly_begin(true)/readonly_end(), assigning each
// batch its own stage via get_stage(slot) and MdRegistryStageScope (see
// md_registry.h) — MdView::each() (the implementation behind every
// MdRegistry::View<T>().each() call) already builds its query once as a
// function-local static, so it satisfies the "built before
// readonly_begin" requirement automatically, with zero call-site changes
// anywhere in the codebase. A batch is safe to co-schedule with another
// IFF, in addition to the disjoint-tags check above: (a) its call tree
// performs zero structural ops (Emplace/Remove/Create/Destroy) while
// running — still true, still not something Conflicts() can see, verify
// by hand; (b) every query iteration in its call tree goes through
// MdRegistry::View<T>().each() (which is now stage-aware) rather than
// MdRegistry::Each() (the MdManagedTag-scoped iterator behind Clear()/
// Count() — NOT yet updated to check the stage override, so a batch that
// calls Clear()/Count() concurrently with another batch is still
// unverified and should stay sequential until that gap is closed).
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
    // warning if violated, same shape as RenderPassGraph::Validate()).
    // Then groups registered batches into waves: a batch's wave is one
    // past the latest-registered batch it Conflicts() with (0 if none) —
    // same-wave batches have no declared conflict with each other. Wave 0
    // (or any wave with just 1 batch) runs directly on the calling
    // thread; a wave with 2+ batches submits each to JobSystem and
    // Flush()es before the next wave starts (see JobBatchFn's caveat
    // above — a disjoint declared tag set alone does not make a batch
    // safe for this). Clears all registered batches when done.
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
