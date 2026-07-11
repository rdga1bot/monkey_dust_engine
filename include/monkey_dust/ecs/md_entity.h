#pragma once
#include <entt/entt.hpp>
#include <cstdint>

// MdEntity — task #8 (EnTT->flecs strangler-fig migration), part B3.1.
//
// Now a real opaque wrapper (was a plain `using MdEntity = entt::entity`
// alias through B1). Backed by entt::entity internally for now; B3.4
// swaps the internal representation to flecs::entity_t in one file once
// every call site has moved onto this type, same strangler-fig pattern
// used for MdRegistry.
//
// Construction from entt::entity is EXPLICIT and there is no implicit
// conversion back — Raw() is the one blessed accessor, mirroring
// MdRegistry::Raw()'s naming. This is deliberate: it turns every one of
// the ~400 raw entt::entity type usages across the codebase into a
// compile error the moment code switches onto MdEntity, giving a
// precise, compiler-verified checklist instead of a hand-audited one —
// this is the follow-through B1's original comment on this file
// promised.
//
// entt::null comparisons work via the explicit, non-template operator==
// overloads below (found via ADL) rather than relying on entt::null_t's
// own templated conversion/comparison operators — those require
// entt_traits<T> to exist for T, which it doesn't for MdEntity, and an
// earlier naive implicit-conversion design caused a real, hard-to-
// diagnose overload-resolution ambiguity (see SquadSystem::
// BroadcastTarget / BTSystem::ConnectRegistry in this session's history:
// both had to be reverted from MdRegistry& back to entt::registry& for
// the same class of reason — entt's generic templates don't gracefully
// accept a type not registered with entt_traits).
class MdEntity {
public:
    MdEntity() = default;
    explicit MdEntity(entt::entity e) : id_(e) {}
    // Raw uint32 entity id — used to rehydrate an entity previously stored
    // via ToIntegral() (e.g. BlackboardEntry::val.e, Lua integer args).
    explicit MdEntity(uint32_t raw) : id_(entt::entity(raw)) {}
    // Implicit and safe: entt::null_t is a concrete, non-template type, so
    // this doesn't hit the entt_traits ambiguity described below — lets
    // `MdEntity x = entt::null;` keep working unchanged.
    MdEntity(entt::null_t) : id_(entt::null) {}

    entt::entity Raw() const { return id_; }
    uint32_t     ToIntegral() const { return entt::to_integral(id_); }

    static MdEntity Null() { return MdEntity(entt::null); }

    friend bool operator==(MdEntity a, MdEntity b) { return a.id_ == b.id_; }
    friend bool operator!=(MdEntity a, MdEntity b) { return a.id_ != b.id_; }
    friend bool operator==(MdEntity a, entt::null_t) { return a.id_ == entt::null; }
    friend bool operator!=(MdEntity a, entt::null_t) { return a.id_ != entt::null; }
    friend bool operator==(entt::null_t, MdEntity a) { return a.id_ == entt::null; }
    friend bool operator!=(entt::null_t, MdEntity a) { return a.id_ != entt::null; }

private:
    entt::entity id_ = entt::null;
};
