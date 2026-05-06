#pragma once
#include <cstdint>

struct PlayerController {
    bool     is_player        = true;
    float    move_speed       = 5.0f;
    float    last_attack_ms   = -1000.0f;
    uint32_t attack_cooldown_ms = 600u;
    uint32_t rng_state        = 55555u;
};
