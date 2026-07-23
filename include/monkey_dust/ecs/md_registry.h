#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/platform/md_log.h>
#include <flecs.h>
#include <tuple>
#include <type_traits>
#include <utility>

// Task #7/#8 concurrency project — real flecs-native multi-threading.
//
// Empirically verified (standalone probes, not guessed) before writing
// this: flecs query iteration (ecs_query_next, i.e. flecs::query::each())
// is NOT safe to run concurrently across 2 threads against the same
// flecs::world under ANY configuration EXCEPT world.readonly_begin(true)
// + each thread iterating a query that was BUILT ONCE against the main
// world BEFORE readonly_begin(), but ITERATED via query.iter(stage_ptr)
// where stage_ptr = world.get_stage(i) — "on-the-fly" query construction
// against a stage, or iterating via the raw (non-stage) world during
// readonly mode, both still crash (SIGSEGV in ecs_iter_fini). Separately
// verified: single-entity get_mut<T>()/try_get<T>() calls on the RAW
// (non-stage) world stay safe even while ANOTHER thread iterates a query
// via its stage — so ONLY query iteration needs stage-routing, not every
// MdRegistry call (confirmed via probe — see CLAUDE_STATE.md for the
// exact probe results).
//
// MdRegistryStageScope is the mechanism: a thread-local override that,
// when set, redirects MdView::each()/MdRegistry::Each() to iterate via
// query.iter(stage) instead of query.each() directly. Default (no scope
// active, the overwhelming majority of the game's execution) is
// byte-for-byte the same code path as before this feature existed — zero
// risk to anything outside the one JobGraph wave that sets it.
namespace md_registry_detail {
    inline thread_local ecs_world_t* t_stage_override = nullptr;
}

// RAII guard: while alive on the calling thread, MdView::each() iterates
// through `stage` instead of the raw world. Nest-safe (restores the prior
// value on destruction, not unconditionally nullptr) though nesting isn't
// expected in practice — one guard per JobGraph batch invocation.
class MdRegistryStageScope {
public:
    explicit MdRegistryStageScope(flecs::world& stage) noexcept
        : prev_(md_registry_detail::t_stage_override) {
        md_registry_detail::t_stage_override = stage.c_ptr();
    }
    ~MdRegistryStageScope() { md_registry_detail::t_stage_override = prev_; }
    MdRegistryStageScope(const MdRegistryStageScope&) = delete;
    MdRegistryStageScope& operator=(const MdRegistryStageScope&) = delete;
private:
    ecs_world_t* prev_;
};

// MdManagedTag — task #8 B3.4. Every entity created via MdRegistry::Create()
// gets this tag, so MdRegistry::Each()/Clear()/Count() can scope "every
// entity I manage" without picking up flecs's own internal bootstrap/module
// entities — flecs's own world-wide entity iteration walks the ENTIRE
// entity index (hundreds of internal IDs for built-in components/modules),
// confirmed empirically; there's no other clean way to ask "just mine."
struct MdManagedTag {};

// MdEach/MdFirst — task #8 Phase 3 (View<T...>() facade removal).
//
// Replace MdView, which hid a flecs::query<T...> behind a header-template
// function-local static — the SAME static, shared across every call site
// for a given T... pack WITHIN one linkage unit, but independently
// duplicated across a dlopen'd .so boundary (a real problem for the two
// hot-reloadable editor/gameplay .so targets: each side would silently
// get its own separate, separately-warmed query object for what looks
// like "the same" MdRegistry::View<T...>() call).
//
// The fix: callers now own an explicit flecs::query<T...> themselves —
// either a plain per-call-site `static auto q = reg.Raw().query<T...>();`
// for call sites with no cross-.so or cross-thread sharing need, or a
// named, non-template accessor function (defined once in a .cpp, e.g.
// game/src/ai/ai_queries.cpp) for the handful of signatures that DO need
// to be the same shared, pre-warmed object everywhere (the concurrent
// JobGraph wave's queries — see ai_queries.h for why). MdEach/MdFirst are
// stateless — safe to call on any flecs::query<T...>, regardless of who
// owns it or where it lives — so they can stay in this header without
// reintroducing the per-.so duplication problem (no static state here).
//
// Stage-awareness (t_stage_override) is preserved exactly as MdView had
// it: whichever query is passed in gets routed through iter(stage) when
// inside a JobGraph-staged batch, or iterated directly otherwise — this
// is what makes concurrent TickNeedsAndInjuries/TickAI query iteration
// safe (see the t_stage_override note above this file's opening comment).
//
// Callback signature: same convention as before — flecs::query::each()
// natively dispatches on whether Func accepts (flecs::entity, T&...) or
// just (T&...), so the wrapping lambda below only needs to convert that
// flecs::entity into MdEntity when the caller's Func wants an MdEntity
// first (checked the same way MdView did).
template<typename Query, typename Func>
void MdEach(Query& q, Func&& func) {
    ecs_world_t* stage = md_registry_detail::t_stage_override;
    auto wrapped = [&func](flecs::entity fe, auto&... args) {
        if constexpr (std::is_invocable_v<Func, MdEntity, decltype(args)&...>) {
            func(MdEntity(fe.id()), args...);
        } else {
            func(args...);
        }
    };
    if (stage) q.iter(stage).each(wrapped);
    else       q.each(wrapped);
}

// First matching entity, or MdEntity::Null() if none — a real early-exit
// via flecs::query::find() (unlike MdView::front(), which had to visit
// every match since flecs's each() has no early-exit protocol). Templated
// on the query's Components... pack (not a generic `auto&...` predicate)
// because flecs::find_delegate resolves the predicate against TWO
// candidate signatures — (flecs::iter&, size_t, Components&...) and plain
// (Components&...) — and a fully generic lambda matches both at once,
// making overload resolution ambiguous.
template<typename... Components>
MdEntity MdFirst(flecs::query<Components...>& q) {
    flecs::entity found = q.find([](Components&...) { return true; });
    return found ? MdEntity(found.id()) : MdEntity::Null();
}

// MdRegistry — task #8 (EnTT->flecs strangler-fig migration), part B3.4.
//
// Thin facade over the SAME flecs::world singleton as Registry::Get() (was
// entt::registry through B1-B3.3) — MdRegistry::Get() wraps Registry::Get(),
// not a second world.
//
// Every method rehydrates a flecs::entity handle from the stored MdEntity's
// raw id via Handle() — flecs::entity handles are cheap (world pointer +
// id, no allocation, no query) so this per-call rehydration costs nothing
// meaningful. Raw() is the intentional escape hatch for code not yet
// retrofitted; View<T...>() caches its underlying query per unique T...
// signature (function-local static, added in B3.3 specifically to prepare
// for this: flecs::query construction registers with the world and is
// meant to be built once and reused, unlike entt::view's near-free
// construction).
//
// CRITICAL, B3.4: Handle(e).get_mut<T>() returns a T& that is only valid
// until the entity's NEXT archetype change — flecs relocates an entity's
// ENTIRE row to a new table on every emplace<U>/set<U>/remove<U> call, even
// for an unrelated component type U, invalidating every previously held T&
// for that entity (unlike entt's per-type-pool-stable references, where
// only the SAME type's pool reallocating could invalidate a ref). Rule:
// call Handle(e).emplace<T>()/set<T>() for every component an entity needs
// FIRST, THEN Handle(e).get_mut<T>() to fetch references for writing.
// get_mut<T>()/try_get_mut<T>() do not themselves invalidate anything (no
// structural change) — freely chaining several in a row is safe. Found and
// fixed 3 real production bugs of this exact shape (see CLAUDE_INVARIANTS.md).
class MdRegistry {
public:
    static MdRegistry& Get() {
        static MdRegistry inst;
        return inst;
    }

    MdEntity Create() {
        auto e = Raw().entity();
        e.add<MdManagedTag>();
        return MdEntity(e.id());
    }
    void Destroy(MdEntity e) { Handle(e).destruct(); }
    // flecs's ecs_is_alive() requires entity != 0 (ecs_check, aborts in
    // debug builds otherwise) — MdEntity::Null() is id_=0, so it must be
    // rejected here before ever reaching is_alive().
    bool Valid(MdEntity e) const {
        return e != MdEntity::Null() && Handle(e).is_alive();
    }

    template<typename T>
    void Remove(MdEntity e) { Handle(e).template remove<T>(); }

    // entt's replace() requires T already present; flecs's set() is a safe
    // upsert either way (verified: does not crash/assert if T is absent),
    // so this is slightly more permissive than the old entt contract but
    // not unsafe.
    template<typename T, typename... Args>
    T& Replace(MdEntity e, Args&&... args) {
        auto h = Handle(e);
        h.template set<T>(T{std::forward<Args>(args)...});
        return h.get_mut<T>();
    }

    template<typename T, typename Fn>
    void Patch(MdEntity e, Fn fn) {
        auto h = Handle(e);
        T& v = h.get_mut<T>();
        fn(v);
        h.template modified<T>();
    }

    // Destroys every MdRegistry-managed entity (tagged MdManagedTag) —
    // does NOT touch flecs's own internal bootstrap/module entities.
    // defer_begin/defer_end: destructing an entity mid-.each() mutates its
    // own table (locked for the duration of iteration) — flecs's internal
    // ecs_assert(!table->_->lock) catches this in debug builds (SIGABRT);
    // in release (NDEBUG) the assert compiles away and the mutation still
    // happens, silently violating "no reg mutation during view.each()".
    // Deferring queues the destructs until after iteration completes.
    // Point 1 (concurrency audit): Clear() is NOT stage-aware (builds a
    // fresh, non-static query and calls .each() directly, bypassing
    // MdEach()'s t_stage_override check) — see job_graph.h's caveat. No
    // current call site reaches Clear() from inside a JobGraph batch (both
    // real callers — editor_toolbar.cpp, scene_serializer.h — are
    // editor-only, main-thread, outside JobGraph::Run()'s window), so this
    // is a latent-risk guard, not a fix for an active bug: it turns future
    // silent UB into a loud signal instead. Deliberately NOT gated behind
    // #ifndef NDEBUG: CLAUDE.md's own documented default build is
    // `-DCMAKE_BUILD_TYPE=Release` (confirmed via build/CMakeCache.txt), so
    // an NDEBUG-gated check would never fire in the configuration this
    // project actually builds/tests with day to day. The check itself is a
    // single pointer compare — negligible next to Clear()'s own query+.each()
    // cost — so there is no real reason to gate it at all.
    //
    // Early-return (not just log-and-continue): Clear()'s body calls
    // defer_begin()/query().each()/defer_end() on the RAW (non-stage) world
    // — running that while a JobGraph wave's readonly_begin(true) is active
    // is exactly the "structural op during readonly mode" hazard flecs's own
    // FLECS_SANITIZE checks (Debug builds, engine/CMakeLists.txt) assert on.
    // Confirmed by running this guard's own test under
    // -DCMAKE_BUILD_TYPE=Debug -DMD_SANITIZE=asan (Phase 4.4 audit): logging
    // alone still let Clear() run its unsafe body and abort — skipping the
    // body once the hazard is detected is the actual fix, not just a louder
    // warning.
    void Clear() {
        if (md_registry_detail::t_stage_override != nullptr) {
            MD_LOG(MD_LOG_ERROR,
                   "[MdRegistry] Clear() called while a JobGraph stage is "
                   "active — Clear()'s query+.each() does not route through "
                   "t_stage_override and will race with concurrent staged "
                   "query iteration. Move this call outside the JobGraph "
                   "batch. Clear() was NOT executed.");
            return;
        }
        auto& w = Raw();
        w.defer_begin();
        w.query<MdManagedTag>().each([](flecs::entity e, MdManagedTag) { e.destruct(); });
        w.defer_end();
    }

    // Reconstruct an MdEntity from a stored uint32 index (e.g.
    // BlackboardEntry::val.e, Lua integer args) — resolves to the
    // CURRENTLY alive entity for that index via flecs's generation-aware
    // lookup, safe against the index having been recycled by a different,
    // later-created entity since the id was stored. Falls back to a
    // generation-0 MdEntity (same as MdEntity(uint32_t) directly) if no
    // alive entity currently holds that index.
    MdEntity FromIndex(uint32_t idx) const {
        ecs_entity_t alive = ecs_get_alive(Raw().c_ptr(), (ecs_entity_t)idx);
        return alive ? MdEntity(alive) : MdEntity(idx);
    }

    flecs::world& Raw() { return Registry::Get(); }
    const flecs::world& Raw() const { return Registry::Get(); }

    // Native flecs::entity handle for MdEntity e — the facade-removal escape
    // hatch (task #8 phase-out): call sites migrating off Emplace/GetOrEmplace/
    // EmplaceOrReplace use Handle(e).emplace<T>()/.set<T>() directly instead
    // of the old T&-returning facade methods, since flecs::entity::emplace/set
    // return the entity itself (not T&), which is what makes the B3.4
    // dangling-reference bug class structurally impossible here.
    flecs::entity Handle(MdEntity e) const { return flecs::entity(Raw(), e.Raw()); }

    // task #8 Phase 5: stage-routed handle for STRUCTURAL ops (emplace/
    // set/destruct/remove) issued from code that might run inside a
    // JobGraph-staged batch (see MdRegistryStageScope above). During
    // world.readonly_begin(true) (which JobGraph::Run() wraps its wave
    // in), flecs forbids structural ops on the main world outright — "readonly
    // assert" — but permits them on a STAGE, where they're automatically
    // queued and merged back into the world on readonly_end() (readonly_
    // begin/end are documented to internally bracket defer_begin/defer_end
    // — see flecs.h's ecs_readonly_begin() doc comment). This is what
    // replaces DeferredStructuralOps' 3 hand-rolled queues: route the
    // structural call through StagedHandle() instead of Handle(), and
    // flecs's own deferred-command mechanism does the rest, generalizing
    // to any future structural op instead of only the 3 that were
    // manually audited and queued before.
    //
    // Falls back to the plain (main-world) Handle() when no stage is
    // active (t_stage_override unset) — i.e. this is always safe to call,
    // inside or outside a JobGraph batch, unlike the old Queue*() calls
    // which only made sense because Flush() ran them through the real
    // structural path afterward.
    //
    // Read-only accessors (Get/TryGet/AllOf/has) are NOT routed through
    // this — the existing get_mut<T>()/try_get<T>() on the RAW world
    // during a concurrent stage's iteration was separately verified safe
    // by probe (see md_registry.h's top-of-file note); only structural
    // ops need the stage.
    //
    // A second, easy-to-miss distinction (empirically verified, standalone
    // flecs probes — red/green-tested against game/src/combat/skill_xp.h's
    // real lost-update bug, see tests/behavior/test_skill_xp_staged_grant.
    // cpp): StagedHandle().set<T>() is genuinely DEFERRED — invisible even
    // through the SAME stage's own reads — ONLY the first time T is added to
    // an entity (a real structural/archetype change). Once T already exists
    // on the entity, a stage-routed set<>() to it is a plain VALUE write and
    // applies immediately, visible to the very next read (staged or main-
    // world) in the same batch — flecs does not defer it at all, because no
    // archetype move is needed. Practical implication for any code called
    // from inside a JobGraph batch: reading a MAIN-world (Handle()) snapshot
    // of T, computing on it, and writing the WHOLE struct back via a single
    // StagedHandle().set<T>() is safe to repeat multiple times per entity
    // per batch ONLY if T already existed on that entity BEFORE the batch
    // started. If T might be getting its first-ever add mid-batch, that
    // pattern silently loses every write but the last one for that entity —
    // prefer MdRegistry::Patch<T>() (in-place mutation via a main-world
    // pointer) once T is known to exist, and treat "T doesn't exist yet" as
    // its own explicit, single, no-computation StagedHandle().set<T>(T{})
    // branch rather than folding it into the same read-compute-write path.
    flecs::entity StagedHandle(MdEntity e) const {
        ecs_world_t* stage = md_registry_detail::t_stage_override;
        return stage ? flecs::entity(stage, e.Raw()) : Handle(e);
    }

    MdRegistry(const MdRegistry&) = delete;
    MdRegistry& operator=(const MdRegistry&) = delete;

private:
    MdRegistry() = default;
};
