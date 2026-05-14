# monkey_dust — Engine

Open-source C++17 game engine library for a Flare-inspired isometric RPG sandbox.
Built around **SDL3 + SDL\_GPU (Vulkan)**, **EnTT ECS**, and a custom stackless **Behavior Tree VM**.

> **Full documentation →** [rdga1bot.github.io/monkey\_dust\_engine/monkey\_dust\_docs.html](https://rdga1bot.github.io/monkey_dust_engine/monkey_dust_docs.html)

---

## Features

### Rendering — SDL\_GPU / Vulkan
| System | Details |
|--------|---------|
| Tile map renderer | FLARE-inspired isometric tiles; TINST instancing (stride=36); billboard + flat-XZ geometry; fringe depth sorting |
| Deferred lighting | PBR IBL + GBuffer (albedo/roughness/normal/metallic); ACES tonemapping |
| Cascaded Shadow Maps | 3-cascade PCF 3×3; GPU compute culling |
| SSAO | Half-res R8 compute pass; 16-tap hemisphere kernel |
| SMAA | 3-pass fullscreen triangle (edge → blend → final) |
| Point / strip lights | Icosphere additive pass; capsule-SDF strip lights |
| GPU skinning | AnimationSoA; SSBO skeletal bones (MAX\_BONES=6); compute dispatch |
| Particles | ParticleSoA CPU-sim; SMOKE/SPARK/BLOOD types |
| Material system | JSON → `GpuPipeline::Desc` auto-builder (OGRE-inspired) |

### AI — Behavior Tree VM
- Stackless BT VM (`BehaviorTree.h`) — 30+ node types, zero heap allocations
- CATHODE patterns (C1–C20) — MotivationType · LogicCharacterFlags · AgentTimerSlot · GaugeType · AwarenessState · AlertnessState · NpcMood · NpcRole · WithdrawState · EntityStateFlag
- BTNodePool — flat 32 KB arena bump-allocator
- BTSystem — 3-phase tick (frame\_flags reset → hint expiry → tree tick)
- BTJsonLoader — recursive strstr parser; 11 enum tables; no external JSON library
- DirectorSystem — menace gauge `[0..1]`; 4 DirectorStages; `NpcConfig::bt_stage[4][32]` per-stage BT overrides
- UtilityScorer — Echo-inspired goal utility; motivation inertia bonus
- NpcMemoryComponent — `SpatialMemory[8]` + `events[8]` POD; no heap
- FlowDurableTrigger — ref-counted durable triggers with duration decay

### ECS — EnTT
14 engine-side components: `WorldTransform` · `AIAgent` · `Health` · `Combat` · `Renderable` · `Building` · `Inventory` · `ProjectileComponent` · `SenseComponent` · `AgentState` · `NpcMemoryComponent` · `BehaviorTreeComponent` · `DirectorHintComponent` · `FlareSpriteAnim`

### Navigation
- Recast/Detour integration; async SPSC pathfinding worker
- `PathCache` with spinlock (`_mm_pause()`); `MAX_PATH_LEN=64`
- NavMesh rebuild enqueue (`EnqueueRebuild`) from BuildSystem/editor

### Audio — miniaudio
- `AudioSystem`: SFX pool (MAX\_SFX=32, BSS); music + ambience streaming
- `AudioHandle` = opaque int — no miniaudio types leak to public headers
- Lua API: `md_play_sfx` / `md_play_music` / `md_play_ambience` / `md_stop_*` / `md_set_volume`

### Scripting — Lua 5.4
- 8 MB custom allocator; io/os/package/debug sandboxed out
- `LuaEventBus` (MAX\_HANDLERS=64 BSS); `FlowGraph` FNV-1a node IDs; ring buffer
- `AgentBlackboard` (MAX\_ENTRIES=24; FNV-1a keys)

### World Simulation
- `WorldSimulation` 1 Hz tick: `FactionState[8]` + `TradeRoute[32]`; gold/prosperity/aggression/population economy
- `FactionSystem` — relation matrix `[-100..100]`; JSON loader
- `BuildSystem` — 200×200 grid; `ProductionChain` (inputs → outputs, timed cycles)
- `SaveSystem` v8 — CRC32 header; async save; `AgentState` inline per NPC record

### Performance
- AVX2 `BulkComputeDistSq` / `BulkComputeLOD` (`_mm256_fmadd_ps`, `alignas(64)` SoA)
- `MD_HOT` / `MD_LIKELY` / `MD_UNLIKELY` / `MD_FORCE_INLINE` compiler hints (`md_hints.h`)
- TINST dirty-flag — skip GPU upload when tiles unchanged
- TransformSoA faction dirty — range upload (not full 65536-slot buffer)
- Hot-reload file watcher (POSIX `stat` + `SDL_Thread`); live BT JSON reload

---

## Target Hardware

Intel HD 520 (Skylake, AVX2) · Vulkan via SDL\_GPU · 4–8 GB shared RAM · 1280×720 · 60 FPS

---

## Build

```bash
# Static library only
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SDL3=ON
ninja -C build monkey_dust_engine

# SPIR-V shaders (after any .vert/.frag/.comp change)
bash scripts/compile_shaders.sh
```

**Dependencies** (bring your own or via CMake FetchContent):
`SDL3` · `EnTT` · `Recast/Detour` · `miniaudio` · `Lua 5.4` · `ImGui 1.92` (for editor builds)

---

## Tests

```bash
ninja -C build monkey_dust_tests
./build/tests/monkey_dust_tests          # 131 tests, 6 suites
```

Suites: FNV, AgentBlackboard, FlowGraph, DirectorSystem, PowerSlotManager, NpcConfig, HotReload, FlowVar, CATHODE-1–10/11–20, BT VM

---

## Repository Layout

```
engine/
  include/monkey_dust/     ← public headers (install target)
    ai/                    ← BT VM, director, sense, utility scorer
    audio/                 ← AudioSystem
    building/              ← BuildSystem, ProductionChain
    combat/                ← damage_calc, hit_zones, power_def
    components/            ← 14 ECS components
    ecs/                   ← Registry (EnTT singleton)
    flare/                 ← tile map, sprite animation, renderer
    math_types.h           ← Vec3/Mat4 (GLM switch -DUSE_GLM)
    nav/                   ← PathCache
    platform/              ← input/audio/window/md_fs/md_log/md_hints
    render/                ← GPU HAL, ring buffer, shadow, SSAO, SMAA …
    save/                  ← SaveSystem v8
    scripting/             ← LuaSystem, LuaEventBus, FlowGraph
    world/                 ← FactionSystem, WorldSimulation, TransformSoA …
  src/                     ← implementation units
  tests/                   ← Google Test suite
```

---

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).

The game itself (`monkey_dust` executable and `game/` sources) is proprietary and not part of this repository.
