#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <monkey_dust/ecs/registry.h>
#include <entt/entt.hpp>
#include <utility>

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
// View<T...>() returns entt's own view type directly rather than wrapping
// it — entt::view::each() supports many lambda signature forms (with/
// without entity, any component order), and reimplementing that
// flexibility would be its own source of bugs for zero benefit right now.
// Raw() is the intentional escape hatch for code not yet retrofitted.
//
// This does not yet hide EnTT (Get<T> still requires an EnTT-registered
// component type, View<T...>() still returns an entt view) — it only
// moves the CALL SITE syntax onto MdRegistry's name. Swapping the
// internal implementation to flecs later (B3) is confined to this one
// file, once every call site has migrated off Registry::Get() directly.
class MdRegistry {
public:
    static MdRegistry& Get() {
        static MdRegistry inst;
        return inst;
    }

    MdEntity Create() { return Raw().create(); }
    void     Destroy(MdEntity e) { Raw().destroy(e); }
    bool     Valid(MdEntity e) const { return Raw().valid(e); }

    template<typename T, typename... Args>
    T& Emplace(MdEntity e, Args&&... args) {
        return Raw().emplace<T>(e, std::forward<Args>(args)...);
    }

    template<typename T>
    T& Get(MdEntity e) { return Raw().get<T>(e); }
    template<typename T>
    const T& Get(MdEntity e) const { return Raw().get<T>(e); }

    template<typename T>
    T* TryGet(MdEntity e) { return Raw().try_get<T>(e); }
    template<typename T>
    const T* TryGet(MdEntity e) const { return Raw().try_get<T>(e); }

    template<typename... T>
    bool AllOf(MdEntity e) const { return Raw().all_of<T...>(e); }
    template<typename... T>
    bool AnyOf(MdEntity e) const { return Raw().any_of<T...>(e); }

    template<typename T>
    void Remove(MdEntity e) { Raw().remove<T>(e); }

    template<typename T, typename... Args>
    T& GetOrEmplace(MdEntity e, Args&&... args) {
        return Raw().get_or_emplace<T>(e, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T& Replace(MdEntity e, Args&&... args) {
        return Raw().replace<T>(e, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T& EmplaceOrReplace(MdEntity e, Args&&... args) {
        return Raw().emplace_or_replace<T>(e, std::forward<Args>(args)...);
    }

    template<typename T, typename Fn>
    void Patch(MdEntity e, Fn fn) { Raw().patch<T>(e, fn); }

    template<typename... T>
    auto View() { return Raw().view<T...>(); }

    void Clear() { Raw().clear(); }

    // Escape hatch for call sites not yet retrofitted onto the typed API
    // above (e.g. entt::to_integral, custom storage<> access).
    entt::registry& Raw() { return Registry::Get(); }
    const entt::registry& Raw() const { return Registry::Get(); }

    MdRegistry(const MdRegistry&) = delete;
    MdRegistry& operator=(const MdRegistry&) = delete;

private:
    MdRegistry() = default;
};
