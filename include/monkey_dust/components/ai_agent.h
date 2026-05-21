#pragma once
#include <cstdint>
#include <entt/entt.hpp>

struct AIAgent {
    uint8_t      lod_level;
    uint32_t     faction_id;
    float        last_tick_ms;
    int8_t       bt_node;
    entt::entity last_attacker    = entt::null;
    int8_t       personal_relation = 0;
    uint8_t      bt_template_id    = 0;  // 255 = schedule NPC
    uint8_t      level             = 1;  // NPC power level [1..255]; scales hp and damage
};
