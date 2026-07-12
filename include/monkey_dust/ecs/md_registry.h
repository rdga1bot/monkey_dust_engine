#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <monkey_dust/ecs/registry.h>
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
    bool Valid(MdEntity e) const { return Handle(e).is_alive(); }

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
    void Clear() {
        Raw().query<MdManagedTag>().each([](flecs::entity e, MdManagedTag) { e.destruct(); });
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

    // B3.4: no-op under flecs. entt::registry::sort<T>() physically
    // reordered a component pool once, benefiting every subsequent view
    // until the next sort call. flecs's equivalent (order_by) is attached
    // to a SPECIFIC query and reapplied lazily each time THAT query
    // iterates — not a one-shot pool-wide reorder — so it isn't a drop-in
    // replacement here. Migrating the actual cache-locality optimization
    // to flecs's per-query order_by model is a deliberate follow-up, not
    // part of B3.4's mechanical backend swap. The one caller
    // (logic_tick.cpp's periodic AIAgent sort) loses this specific
    // micro-optimization but stays functionally correct.
    template<typename T, typename Compare>
    void Sort(Compare) {}

    flecs::world& Raw() { return Registry::Get(); }
    const flecs::world& Raw() const { return Registry::Get(); }

    // Native flecs::entity handle for MdEntity e — the facade-removal escape
    // hatch (task #8 phase-out): call sites migrating off Emplace/GetOrEmplace/
    // EmplaceOrReplace use Handle(e).emplace<T>()/.set<T>() directly instead
    // of the old T&-returning facade methods, since flecs::entity::emplace/set
    // return the entity itself (not T&), which is what makes the B3.4
    // dangling-reference bug class structurally impossible here.
    flecs::entity Handle(MdEntity e) const { return flecs::entity(Raw(), e.Raw()); }

    MdRegistry(const MdRegistry&) = delete;
    MdRegistry& operator=(const MdRegistry&) = delete;

private:
    MdRegistry() = default;
};
