#pragma once
#include <cstdint>

// Tag component for entities spawned from a FlareSpawn block (FlareRuntime::LoadMod).
// Identifies the enemy category so AISystem, BillboardRenderer, and save logic
// can distinguish Flare-origin actors from procedurally spawned NPCs.
struct FlareActorComponent {
    char category[32];   // enemy filename stem, e.g. "goblin", "minotaur"
    int  level;          // from FlareSpawn::level (0 = default)
    int  wander_radius;  // tile-radius wander zone from spawn center
};
