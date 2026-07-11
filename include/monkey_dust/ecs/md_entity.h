#pragma once
#include <flecs.h>
#include <entt/entt.hpp>
#include <cstdint>

// MdEntity — task #8 (EnTT->flecs strangler-fig migration), part B3.4.
//
// Backed by flecs::entity_t (raw uint64_t) now — was entt::entity through
// B1-B3.3. Construction from flecs::entity_t is EXPLICIT and there is no
// implicit conversion back — Raw() is the one blessed accessor.
//
// entt::null_t is still used as MdEntity's null-token convention
// (`MdEntity x = entt::null;`) — this was a deliberate B3.4 call, not an
// oversight: ~90 call sites across the codebase use `entt::null` purely as
// a zero-cost sentinel-TYPE default, never touching entt::registry
// internals, so keeping this one type as the null convention avoided
// rewriting all of them. flecs itself uses 0 as its invalid-entity value
// (unlike entt's all-1s convention) — id_'s default/null value is 0 to
// match flecs's own convention (see transform_soa.cpp's bulk-init memset,
// which fills 0x00 now, not 0xFF).
//
// entt::null comparisons work via the explicit, non-template operator==
// overloads below (found via ADL) rather than relying on entt::null_t's
// own templated conversion/comparison operators — those require
// entt_traits<T> to exist for T, which it doesn't for MdEntity, and an
// earlier naive implicit-conversion design caused a real, hard-to-
// diagnose overload-resolution ambiguity.
//
// MdEntity(uint32_t) reconstructs from just the low 32 bits (generation
// defaults to 0) — a "dumb", world-independent bit-reconstruction. flecs
// entity_t packs a 32-bit index in the low bits and a 32-bit generation in
// the high bits; if the original entity died and its index got recycled by
// a newer entity, this constructor does NOT recover that — it's the raw,
// no-world-access fallback. MdRegistry::FromIndex(uint32_t) is the
// world-aware equivalent (via ecs_get_alive) and is what call sites that
// round-trip an entity through a uint32_t (BlackboardEntry::val.e, Lua
// integer args) should actually use.
class MdEntity {
public:
    MdEntity() = default;
    explicit MdEntity(flecs::entity_t e) : id_(e) {}
    explicit MdEntity(uint32_t raw_index) : id_(static_cast<flecs::entity_t>(raw_index)) {}
    MdEntity(entt::null_t) : id_(0) {}

    flecs::entity_t Raw() const { return id_; }
    uint32_t        ToIntegral() const { return static_cast<uint32_t>(id_); }

    static MdEntity Null() { return MdEntity(entt::null); }

    friend bool operator==(MdEntity a, MdEntity b) { return a.id_ == b.id_; }
    friend bool operator!=(MdEntity a, MdEntity b) { return a.id_ != b.id_; }
    friend bool operator==(MdEntity a, entt::null_t) { return a.id_ == 0; }
    friend bool operator!=(MdEntity a, entt::null_t) { return a.id_ != 0; }
    friend bool operator==(entt::null_t, MdEntity a) { return a.id_ == 0; }
    friend bool operator!=(entt::null_t, MdEntity a) { return a.id_ != 0; }

private:
    flecs::entity_t id_ = 0;
};
