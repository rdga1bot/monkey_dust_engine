# monkey_dust/engine — universal core (Flare-engine analog)

> **Головний документ проекту:** `CLAUDE_CONSTITUTION.md` у приватному репо `monkey_dust/`.
> Всі правила §2.2 (заборони, стандарти коду, архітектурні інваріанти) діють тут повністю.

## Mission
Game-agnostic foundation: render (PBR/CSM/skinning), nav (Recast), ECS (EnTT),
BT VM, scripting (Lua), platform abstraction (SDL3/audio/input/math).
**ZERO knowledge of gameplay.** No HitZones, no Quests, no Powers, no Factions.

## Public API surface
- `<monkey_dust/monkey_dust.h>` umbrella header
- `md::EngineContext` — base context struct; game inherits it in GameState.
- `md::BTActionRegistry::Get()` — extension point for game BT functions.
- `LuaSystem::RegisterFunction()` — extension point for game Lua bindings.
- `ChunkManager::SetSpawnCallbacks()` — extension point for game entity spawning.
- `SaveSystem::SetCallbacks()` — extension point for game save/load.

## Forbidden (in addition to global §2.2 constraints)
- **DO NOT** `#include` anything from `game/` or `tools/`. CORE split-readiness rule.
- **DO NOT** hardcode component names (Health, AIAgent, Faction…) — those are game.
- **DO NOT** add game-specific constants (MAX_QUESTS, MAX_FACTIONS) — those are game.

## Build
```bash
cmake -S . -B build -DMONKEY_DUST_BUILD_GAME=OFF -DMONKEY_DUST_BUILD_TOOLS=OFF
ninja -C build monkey_dust_engine
```
Output: `libmonkey_dust_engine.a` + headers in `engine/include/monkey_dust/`.

## Recently added (DECOUPLE-1, 2026-06-05)
- `engine/include/monkey_dust/world/biome_def.h` — BiomeDef struct + kBiomeTable[] + ForZone()
- `engine/include/monkey_dust/world/world_registry.h` + `engine/src/world/world_registry.cpp`
  — WorldRegistry singleton; LoadFromTerrainConfig(), GenerateSettlements()
  — moved from `game/src/world/` (previously game-only, now engine-owned)

## CI checkpoint (split-readiness)
```bash
grep -r '#include.*"\.\./game/' engine/   # MUST BE EMPTY
grep -r '#include.*"\.\./tools/' engine/  # MUST BE EMPTY
```
