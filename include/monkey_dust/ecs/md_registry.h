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

// MdView — task #8 (EnTT->flecs strangler-fig migration), parts B3.1-B3.4.
//
// Wraps a flecs::query<T...> so MdRegistry::View<T...>() can hand out
// MdEntity-typed entities to callback lambdas without every call site
// changing. flecs::query::each(Func) dispatches on whether Func's first
// param is flecs::entity — since that check targets the REAL flecs::entity
// type, and our own each(Func) wraps into a lambda whose first param IS a
// real flecs::entity (not MdEntity), flecs's own dispatch keeps working;
// each(Func) below only needs to decide whether to wrap-and-convert to
// MdEntity or pass the user's Func straight through unmodified.
//
// No zero-argument structured-binding form here (unlike B3.1-era MdView) —
// flecs queries have no entt-style "range of tuples" each() overload, only
// the callback form, and nothing in the codebase used the zero-arg form
// directly (the handful of structured-binding call sites all went through
// .Raw().each() instead, which those call sites were rewritten off of in
// B3.4 since flecs simply doesn't support that shape).
template<typename Query, typename... T>
class MdView {
public:
    explicit MdView(Query q) : query_(q) {}

    template<typename Func>
    void each(Func func) {
        ecs_world_t* stage = md_registry_detail::t_stage_override;
        if constexpr (std::is_invocable_v<Func, MdEntity, T&...>) {
            auto wrapped = [&func](flecs::entity e, T&... args) { func(MdEntity(e.id()), args...); };
            if (stage) query_.iter(stage).each(wrapped);
            else       query_.each(wrapped);
        } else {
            if (stage) query_.iter(stage).each(func);
            else        query_.each(func);
        }
    }

    bool empty() { return query_.count() == 0; }

    // First matching entity, or MdEntity::Null() if none. flecs queries
    // have no early-exit each() — this always visits every match, but its
    // only 2 callers are editor/dialog code, not hot path.
    MdEntity front() {
        MdEntity result = MdEntity::Null();
        bool found = false;
        query_.each([&](flecs::entity e, T&...) {
            if (!found) { result = MdEntity(e.id()); found = true; }
        });
        return result;
    }

    // Escape hatch: raw flecs query, for anything not covered above.
    Query& Raw() { return query_; }

private:
    Query query_;
};

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
// CRITICAL, B3.4: Emplace<T>()/GetOrEmplace<T>() return a T& that is only
// valid until the entity's NEXT archetype change — flecs relocates an
// entity's ENTIRE row to a new table on every Emplace<U>/Remove<U> call,
// even for an unrelated component type U, invalidating every previously
// held T& for that entity (unlike entt's per-type-pool-stable references,
// where only the SAME type's pool reallocating could invalidate a ref).
// Rule: Emplace() every component an entity needs FIRST, THEN Get<T>() to
// fetch references for writing. Get<T>()/TryGet<T>() do not themselves
// invalidate anything (no structural change) — freely chaining several
// Get<>() calls in a row is safe. Found and fixed 2 real production bugs
// of this exact shape during B3.4 verification (see CLAUDE_INVARIANTS.md).
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

    template<typename T, typename... Args>
    T& Emplace(MdEntity e, Args&&... args) {
        auto h = Handle(e);
        h.emplace<T>(std::forward<Args>(args)...);
        return h.get_mut<T>();
    }

    template<typename T>
    T& Get(MdEntity e) { return Handle(e).get_mut<T>(); }
    template<typename T>
    const T& Get(MdEntity e) const { return Handle(e).get<T>(); }

    template<typename T>
    T* TryGet(MdEntity e) { return Handle(e).template try_get_mut<T>(); }
    template<typename T>
    const T* TryGet(MdEntity e) const { return Handle(e).template try_get<T>(); }

    template<typename... T>
    bool AllOf(MdEntity e) const { auto h = Handle(e); return (h.template has<T>() && ...); }
    template<typename... T>
    bool AnyOf(MdEntity e) const { auto h = Handle(e); return (h.template has<T>() || ...); }

    template<typename T>
    void Remove(MdEntity e) { Handle(e).template remove<T>(); }

    template<typename T, typename... Args>
    T& GetOrEmplace(MdEntity e, Args&&... args) {
        auto h = Handle(e);
        if (!h.template has<T>()) h.template emplace<T>(std::forward<Args>(args)...);
        return h.get_mut<T>();
    }

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

    template<typename T, typename... Args>
    T& EmplaceOrReplace(MdEntity e, Args&&... args) {
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

    template<typename... T>
    auto View() {
        static auto q = Raw().query<T...>();
        return MdView<decltype(q), T...>(q);
    }

    // Destroys every MdRegistry-managed entity (tagged MdManagedTag) —
    // does NOT touch flecs's own internal bootstrap/module entities.
    void Clear() {
        Raw().query<MdManagedTag>().each([](flecs::entity e, MdManagedTag) { e.destruct(); });
    }

    // Iterate every MdRegistry-managed entity. If func returns bool,
    // returning false stops iteration early is NOT supported here (flecs's
    // query.each() has no early-exit callback protocol) — func always
    // visits every managed entity; callers wanting a cap just no-op past
    // their limit inside func.
    template<typename Func>
    void Each(Func func) {
        static auto q = Raw().query<MdManagedTag>();
        q.each([&](flecs::entity e, MdManagedTag) { func(MdEntity(e.id())); });
    }

    size_t Count() {
        static auto q = Raw().query<MdManagedTag>();
        return (size_t)q.count();
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

    MdRegistry(const MdRegistry&) = delete;
    MdRegistry& operator=(const MdRegistry&) = delete;

private:
    MdRegistry() = default;

    flecs::entity Handle(MdEntity e) const { return flecs::entity(Raw(), e.Raw()); }
};
