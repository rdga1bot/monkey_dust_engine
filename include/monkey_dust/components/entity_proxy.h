#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <cstdint>
#include <entt/entt.hpp>

// EntityProxyComponent — MD EntityProxy pattern:
// Lightweight stand-in for a streamed-out entity.
// Holds template GUID (FNV-1a) + desired spawn position.
// SpawnState tracks the lazy-load lifecycle.
// Game systems (ChunkManager) check state to decide when to materialise.

enum class ProxySpawnState : uint8_t {
    Dormant  = 0,  // not yet requested
    Pending  = 1,  // spawn requested, not yet fulfilled
    Spawned  = 2,  // live entity exists (live_entity is valid)
    Failed   = 3,  // spawn attempt failed; will retry
};

struct EntityProxyComponent {
    uint32_t       template_hash;   // fnv1a of template/prefab name
    float          x, z;            // desired world spawn position
    MdEntity   live_entity;     // valid when state == Spawned
    ProxySpawnState state;
    uint8_t        retry_count;
    uint8_t        _pad[2];
};
static_assert(sizeof(EntityProxyComponent) == 20);

// Helpers
inline bool proxy_is_live(const EntityProxyComponent& p) noexcept {
    return p.state == ProxySpawnState::Spawned &&
           p.live_entity != entt::null;
}
inline void proxy_request_spawn(EntityProxyComponent& p) noexcept {
    if (p.state == ProxySpawnState::Dormant)
        p.state = ProxySpawnState::Pending;
}
inline void proxy_on_spawned(EntityProxyComponent& p, MdEntity live) noexcept {
    p.live_entity = live;
    p.state       = ProxySpawnState::Spawned;
    p.retry_count = 0;
}
inline void proxy_on_despawned(EntityProxyComponent& p) noexcept {
    p.live_entity = entt::null;
    p.state       = ProxySpawnState::Dormant;
}
