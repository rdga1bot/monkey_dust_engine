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
// On trigger: destination_zone loads; player teleports to spawn.

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

// W-2: SharedInteriorsComponent — cross-building interior linking.
// Kenshi RE: "shares interiors with" — vector<ref> stride=0x40 per entry.
// Multiple buildings sharing one interior chunk (e.g. warehouse complex).
// Attach to building entities that share interior space.
static constexpr int MAX_SHARED_INTERIORS = 8;

struct SharedInteriorsComponent {
    uint16_t  zone_ids[MAX_SHARED_INTERIORS] = {};  // dest_zone_id of each linked interior
    uint8_t   count = 0;
    uint8_t   _pad[7];
};
static_assert(sizeof(SharedInteriorsComponent) == 24, "SharedInteriorsComponent size");

// W-3: InteriorLoadState — progressive interior chunk load depth tracker.
// Kenshi RE: "interiors.level" depth counter; *(param_1+0x180) = done flag.
// When load_depth increases, the next level of interior zones loads.
struct InteriorLoadState {
    uint8_t  load_depth  = 0;   // current load depth (0=not loaded, 1=surface, 2+=deep)
    uint8_t  load_done   = 0;   // 1 = all levels at current depth loaded
    uint8_t  target_depth= 1;   // requested depth (set by portal trigger)
    uint8_t  _pad        = 0;
};
static_assert(sizeof(InteriorLoadState) == 4, "InteriorLoadState size");
