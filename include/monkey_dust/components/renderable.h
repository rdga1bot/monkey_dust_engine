#pragma once
#include <cstdint>

struct Renderable {
    uint8_t sprite_id;
    uint8_t lod_level;
    bool    visible;
};
