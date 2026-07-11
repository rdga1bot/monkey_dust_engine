#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <monkey_dust/ecs/registry.h>
#include <entt/entt.hpp>
#include <tuple>
#include <type_traits>
#include <utility>

// MdView — task #8 (EnTT->flecs strangler-fig migration), parts B3.1/B3.2.
//
// Wraps an entt view so MdRegistry::View<T...>() can hand out MdEntity-
// typed entities to callback lambdas without every call site changing.
// entt::view::each(Func) picks its dispatch form via
// is_invocable_v<Func, entt::entity, T&...> — since MdEntity's ctor from
// entt::entity is deliberately explicit (see md_entity.h), that check
// fails for `[](MdEntity e, T&...)` lambdas and entt silently falls back
// to the component-only form, breaking the call. each(Func) below does
// its own is_invocable_v check keyed on MdEntity instead, and either
// wraps-and-forwards or passes the lambda straight through.
//
// each() (zero-arg, B3.2) supports the structured-binding form
// `for (auto [e, comps...] : view.each())` — entt's own no-arg each()
// yields entt::entity by value in the tuple's first slot; EachRange
// wraps its iterator to convert that to MdEntity on dereference.
template<typename RawView, typename... T>
class MdView {
public:
    explicit MdView(RawView v) : view_(v) {}

    template<typename Func>
    void each(Func func) {
        if constexpr (std::is_invocable_v<Func, MdEntity, T&...>) {
            view_.each([&func](entt::entity e, T&... args) { func(MdEntity(e), args...); });
        } else {
            view_.each(func);
        }
    }

    class EachRange {
    public:
        explicit EachRange(RawView& v) : range_(v.each()) {}

        class Iterator {
        public:
            using RawIt = decltype(std::declval<decltype(std::declval<RawView&>().each())&>().begin());
            explicit Iterator(RawIt it) : it_(it) {}
            auto operator*() const {
                return std::apply([](entt::entity e, T&... args) {
                    return std::tuple<MdEntity, T&...>(MdEntity(e), args...);
                }, *it_);
            }
            Iterator& operator++() { ++it_; return *this; }
            bool operator!=(const Iterator& o) const { return it_ != o.it_; }
        private:
            RawIt it_;
        };

        Iterator begin() { return Iterator(range_.begin()); }
        Iterator end()   { return Iterator(range_.end()); }
    private:
        decltype(std::declval<RawView&>().each()) range_;
    };

    EachRange each() { return EachRange(view_); }

    bool empty() { return view_.empty(); }
    MdEntity front() { return MdEntity(view_.front()); }

    // Escape hatch: raw entt view, for anything not covered above.
    RawView& Raw() { return view_; }

private:
    RawView view_;
};

// MdRegistry — task #8 (EnTT->flecs strangler-fig migration), part B1 step 2.
//
// Thin facade over the SAME entt::registry singleton as Registry::Get() —
// MdRegistry::Get() wraps Registry::Get(), not a second registry, so
// retrofitted and not-yet-retrofitted call sites stay perfectly in sync
// during the transition. Every method here mirrors entt::registry's own
// templated shape (Get<T>, View<T...>, ...) 1:1, so a call site rename
// from `Registry::Get().foo<T>(e)` to `MdRegistry::Get().Foo<T>(e)` is a
// pure name change with identical runtime behavior today.
//
// View<T...>() returns an MdView<T...> (see above) rather than entt's own
// view type directly — this lets each(Func) hand callback lambdas an
// MdEntity instead of a raw entt::entity. Raw() is the intentional escape
// hatch for code not yet retrofitted.
//
// This does not yet hide EnTT (Get<T> still requires an EnTT-registered
// component type, View<T...>() still wraps an entt view) — it only moves
// the CALL SITE syntax onto MdRegistry's name. Swapping the internal
// implementation to flecs later (B3) is confined to this one file, once
// every call site has migrated off Registry::Get() directly.
class MdRegistry {
public:
    static MdRegistry& Get() {
        static MdRegistry inst;
        return inst;
    }

    MdEntity Create() { return MdEntity(Raw().create()); }
    void     Destroy(MdEntity e) { Raw().destroy(e.Raw()); }
    bool     Valid(MdEntity e) const { return Raw().valid(e.Raw()); }

    template<typename T, typename... Args>
    T& Emplace(MdEntity e, Args&&... args) {
        return Raw().emplace<T>(e.Raw(), std::forward<Args>(args)...);
    }

    template<typename T>
    T& Get(MdEntity e) { return Raw().get<T>(e.Raw()); }
    template<typename T>
    const T& Get(MdEntity e) const { return Raw().get<T>(e.Raw()); }

    template<typename T>
    T* TryGet(MdEntity e) { return Raw().try_get<T>(e.Raw()); }
    template<typename T>
    const T* TryGet(MdEntity e) const { return Raw().try_get<T>(e.Raw()); }

    template<typename... T>
    bool AllOf(MdEntity e) const { return Raw().all_of<T...>(e.Raw()); }
    template<typename... T>
    bool AnyOf(MdEntity e) const { return Raw().any_of<T...>(e.Raw()); }

    template<typename T>
    void Remove(MdEntity e) { Raw().remove<T>(e.Raw()); }

    template<typename T, typename... Args>
    T& GetOrEmplace(MdEntity e, Args&&... args) {
        return Raw().get_or_emplace<T>(e.Raw(), std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T& Replace(MdEntity e, Args&&... args) {
        return Raw().replace<T>(e.Raw(), std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T& EmplaceOrReplace(MdEntity e, Args&&... args) {
        return Raw().emplace_or_replace<T>(e.Raw(), std::forward<Args>(args)...);
    }

    template<typename T, typename Fn>
    void Patch(MdEntity e, Fn fn) { Raw().patch<T>(e.Raw(), fn); }

    // B3.3: the underlying view is cached per unique T... signature (one
    // function-local static per template instantiation) rather than
    // rebuilt on every call. For entt this is a no-op today — a view is
    // just storage<T>* pointers, cheap to fetch and stable across
    // Registry::Clear() (clear() empties storage in place, it doesn't
    // destroy/recreate the pool) — so a cached view still iterates
    // whatever's live at call time. This is prep for B3.4: flecs::query
    // <T...> registers with the world at construction and is meant to be
    // built once and reused, not rebuilt every call; caching now means
    // the swap in B3.4 doesn't also have to introduce caching under it.
    template<typename... T>
    auto View() {
        static auto v = Raw().view<T...>();
        return MdView<decltype(v), T...>(v);
    }

    void Clear() { Raw().clear(); }

    // B3.2: iterate every live entity in the registry (entt's
    // storage<entt::entity>() idiom), yielding MdEntity. Replaces the
    // `reg.Raw().storage<entt::entity>()` escape hatch call sites.
    // If func returns bool, returning false stops iteration early
    // (find-first pattern); a void-returning func always visits all.
    template<typename Func>
    void Each(Func func) {
        for (auto e : Raw().storage<entt::entity>()) {
            if constexpr (std::is_invocable_r_v<bool, Func, MdEntity>) {
                if (!func(MdEntity(e))) break;
            } else {
                func(MdEntity(e));
            }
        }
    }

    // Count of live entities — mirrors entt's storage<entt::entity>().size().
    size_t Count() { return Raw().storage<entt::entity>().size(); }

    // B3.2: reorder a component pool (e.g. for cache-friendly iteration by
    // distance) — entt has no MdEntity-typed equivalent, so Compare still
    // takes entt::entity; wrap MdEntity(a)/MdEntity(b) inside if needed.
    template<typename T, typename Compare>
    void Sort(Compare cmp) { Raw().sort<T>(cmp); }

    // Escape hatch for call sites not yet retrofitted onto the typed API
    // above (e.g. entt::to_integral, custom storage<> access).
    entt::registry& Raw() { return Registry::Get(); }
    const entt::registry& Raw() const { return Registry::Get(); }

    MdRegistry(const MdRegistry&) = delete;
    MdRegistry& operator=(const MdRegistry&) = delete;

private:
    MdRegistry() = default;
};
