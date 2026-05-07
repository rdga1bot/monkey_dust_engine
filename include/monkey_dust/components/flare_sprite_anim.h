#pragma once
#include <monkey_dust/flare/sprite_animation.h>

// Per-entity sprite animation state for Flare-origin actors.
// Managed by FlareAnimSystem; requires a paired FlareActorComponent.
// atlas_slot is -1 until FlareAnimSystem::Init() assigns it.
struct FlareSpriteAnim {
    md::flare::SpriteAnimState anim       = {};
    int8_t                     atlas_slot = -1;
    uint8_t                    _pad[3]    = {};
};
