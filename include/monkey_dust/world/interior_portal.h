#pragma once
// InteriorPortal — axis-aligned trigger volume that transitions the player
// between exterior and interior zones (buildings, caves, etc.).
//
// Kenshi RE: each interior = separate chunk loaded on demand;
//   portal is a collision trigger → load interior chunk, teleport player to
//   spawn_x/z inside, disable exterior chunk rendering.
//
// Usage (attach to a building entity):
//   reg.emplace<InteriorPortal>(door_entity, portal);
//
// InteriorSystem::Tick() checks player proximity each logic tick.
// On trigger: ChunkManager loads destination_zone; player teleport to spawn.

#include <cstdint>

static constexpr float PORTAL_TRIGGER_RADIUS = 1.5f;  // metres

struct InteriorPortal {
    // Trigger box centre (world space)
    float    trigger_x;
    float    trigger_z;
    float    trigger_half_w;   // half-width X
    float    trigger_half_d;   // half-depth Z

    // Destination zone and spawn position
    uint16_t dest_zone_id;     // 0 = exterior (exit portal)
    uint16_t src_zone_id;      // zone this portal belongs to (for back-link)
    float    spawn_x;
    float    spawn_z;

    // Runtime state (not serialized)
    uint8_t  triggered;        // 1 = player just entered (debounce)
    uint8_t  is_exit;          // 1 = this portal leads back to exterior
    uint8_t  _pad[2];

    // Returns true if (px, pz) is inside the trigger box.
    bool Contains(float px, float pz) const noexcept {
        float dx = px - trigger_x;
        float dz = pz - trigger_z;
        return (dx >= -trigger_half_w && dx <= trigger_half_w &&
                dz >= -trigger_half_d && dz <= trigger_half_d);
    }
};
static_assert(sizeof(InteriorPortal) == 32, "InteriorPortal size");
