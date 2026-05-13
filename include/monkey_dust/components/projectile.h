#pragma once
#include <entt/entt.hpp>

struct ProjectileComponent {
    entt::entity owner;      // caster entity (excluded from hit checks)
    int          power_id;   // source power (for FX lookup)
    float        x, z;       // current world position (mirrored to WorldTransform)
    float        vx, vz;     // velocity m/s
    float        speed;      // scalar speed m/s
    float        damage;
    float        lifespan_s;
    float        elapsed_s;
    float        radius;     // hit sphere radius (m)
};
