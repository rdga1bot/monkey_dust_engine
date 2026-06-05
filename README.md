# monkey_dust — Engine

Open-source C++17 game engine library for a Flare-inspired isometric RPG sandbox.
Built around **SDL3 + SDL\_GPU (Vulkan)**, **EnTT ECS**, a custom stackless **Behavior Tree VM**, **ozz-animation**, and **Jolt Physics**.

> **Full documentation →** [rdga1bot.github.io/monkey\_dust\_engine/monkey\_dust\_docs.html](https://rdga1bot.github.io/monkey_dust_engine/monkey_dust_docs.html)

---

## Features

### Rendering — SDL\_GPU / Vulkan
| System | Details |
|--------|---------|
| Tile map renderer | FLARE-inspired isometric tiles; TINST stride=36; `uint64_t` depth sort (eliminates Z-fight at tile edges); billboard + flat-XZ; fringe layers; `fadeOverlapTile` (player under roof = 35 % alpha); 4-phase pipeline (`SetObjectLayerIdx`) |
| OIT | 2-MRT weighted blended OIT: RGBA16F accum + R8G8B8A8 revealage; **compute composite** (avoids Intel HD 520 driver crash with 2+ fragment samplers); depth test against opaque scene |
| Deferred lighting | GBuffer 2-RT (RT0=albedo+rough, RT1=oct-normal+metallic+flags); ambient pass `DeferredLightingSystem` → RGBA16F hdr\_color; point volumes; strip lights; ACES tonemapping |
| Cascaded Shadow Maps | 3-cascade **EVSM** soft shadows; texel-snap (eliminates shimmer); 10 % cascade blend overlap; GPU compute culling (`shadow_cull.comp`) |
| SSAO | Half-res R8 compute pass; 16-tap hemisphere kernel |
| SMAA | 3-pass fullscreen triangle (edge → blend → final) |
| Point / strip lights | Icosphere additive pass; capsule-SDF strip lights |
| GPU skinning | AnimationSoA; SSBO skeletal bones (MAX\_BONES=6); compute dispatch |
| Particles | ParticleSoA CPU-sim; SMOKE/SPARK/BLOOD types |
| Material system | O3DE-inspired: JSON → `GpuPipeline::Desc`; **parent inheritance** (`"parent": "base_pbr"`); `shader_features` bitmask; `MaterialTypeRegistry` (MAX=32) |
| Terrain POM | Parallax Occlusion Mapping + self-shadow; per-vertex geomorphing L0→L1; `DrawRaw(int lod)` / `DrawRawPOM(int lod)` — lod=0 full-res (64×64), lod=1/2/3 uses `chunk.ibo_lod[lod-1]` (32/16/8 quads); POM only at lod=0; editor uses distance-based per-chunk LOD |

### AI — Behavior Tree VM
- Stackless BT VM (`behavior_tree.h`, 373 lines core VM) + `bt_types.h` (987 lines — all enums/structs: BTNodeType, BTNode, BTState, etc.) + `bt_factories.cpp` (868 lines — Batch 2–35 factory methods) — 30+ node types, zero heap allocations
- Extended AI patterns (C1–C20) — MotivationType · LogicCharacterFlags · AgentTimerSlot · GaugeType · AwarenessState · AlertnessState · NpcMood · NpcRole · WithdrawState · EntityStateFlag
- BTNodePool — flat 32 KB arena bump-allocator
- BTSystem — 3-phase tick (frame\_flags reset → hint expiry → tree tick)
- BTJsonLoader — recursive strstr parser; 11 enum tables; no external JSON library
- NPC Archetypes (M54) — Guard/Alien/Vendor `BuildXxxBT()` helpers; breadth-first child ordering
- SenseSystem (M55) — `SenseSystemUpdate`; visual cone (dist+half-angle); audio linear 15 m falloff; rising-edge → `last_activated_ms` / `last_known_x/z`
- CombatDispatch (M56) — `ff::SHOULD_MELEE_ATTACK`/`SHOULD_RANGED_SHOOT`; target from AgentBlackboard; CalcDamage+RollHitZone; IS_DEAD on kill
- DirectorSystem — menace gauge `[0..1]`; 4 DirectorStages; `NpcConfig::bt_stage[4][32]` per-stage BT overrides
- UtilityScorer — Echo-inspired goal utility; motivation inertia bonus
- NpcMemoryComponent — `SpatialMemory[8]` + `events[8]` POD; no heap
- NpcInteractionComponent (M58) — `dialog_faction_id` + `interaction_range` (2.5 m) + `cooldown_ms`; 20 bytes
- FlowDurableTrigger — ref-counted durable triggers with duration decay

### ECS — EnTT
15 engine-side components: `WorldTransform` · `AIAgent` · `Health` · `Combat` · `Renderable` · `Building` · `Inventory` · `ProjectileComponent` · `SenseComponent` · `AgentState` · `NpcMemoryComponent` · `BehaviorTreeComponent` · `DirectorHintComponent` · `FlareSpriteAnim` · `NpcInteractionComponent`

### Physics & Animation
- **Jolt Physics** — `JoltWorld`: `CharacterVirtual` (max_bodies=512); `TempAllocatorImpl` 8 MB
- **ozz-animation** — `OzzAnimPlayer`: blend trees, `Eval/EvalBlend`; 30-joint Biped rig
- **DetourCrowd** — `CrowdSystem` ORCA; max_agents=64
- **Foot IK** (M59) — analytic 2-bone foot placement; TerrainQuery ray-cast

### Terrain
- `TerrainQuery` singleton — single source of truth for terrain heights/normals/slopes
- `TerrainAtlas` — `world_hmap.r32` (67 MB, 4096 zones 65×65); O(1) RAM lookup; dirty-zone partial save
- **`TerrainAtlas_SmoothBoundaries()`** — N=15 kernel blends zone boundary heights (eliminates Kenshi fullmap 22m+ height-jump seams → NdotL cliffs)
- Cross-chunk normal stitching via atlas (no file I/O, no seam artefacts)
- **`ndl_min=0.57`** in `terrain_pom.frag` — floor = flat-terrain NdotL; zone-boundary faces match flat ground (no warm/cool split)
- `TerrainGen_Build` / `TerrainGen_Upload` — worker-thread mesh gen + GPU upload
- **Parallax Occlusion Mapping** — `terrain_pom.vert/frag`; linear search + binary refinement; self-shadowing with view-angle attenuation; 15 m distance cutoff; detail tile 8×; `height_scale=0.04`
- **Mesh LOD** — 4 levels (64/32/16/8 quads) via separate IBOs per chunk; distance thresholds 600/1200/2000 m; `TERRAIN_LOD_DIST[3]` / `TERRAIN_LOD_IDX[3]`
- **Per-vertex geomorphing** — `morph_y` field in `TerrainVertex` (stride 48→52); L0→L1 blend zone 420–600 m computed per-vertex in `terrain_pom.vert`; eliminates LOD pop
- **`PoissonScatter`** — Bridson O(n) Poisson Disk Sampling for prop placement; min-distance guarantee; slope + embed constraints; deterministic seed; used for mixed rock+vegetation scatter

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
- **`MdEventScheduler`** — Flare EventComponent-inspired: OneShot / Cooldown / Delayed / Repeating / FireOnLoad / FireOnClear; `RequestFire()` for manual cooldown trigger

### World Simulation
- `WorldSimulation` 1 Hz tick: `FactionState[8]` + `TradeRoute[32]`; gold/prosperity/aggression/population economy
- `FactionSystem` — relation matrix `[-100..100]`; JSON loader
- `BuildSystem` — 200×200 grid; `ProductionChain` (inputs → outputs, timed cycles)
- **`SettlementPlacer`** — procedural settlement generation: stratified grid sampling + flatness scoring + biome weights; greedy 500 m minimum gap between sites; `FactionVoronoi` assignment by nearest faction seed; returns `SettlementCandidate[]` sorted by score
- `SaveSystem` v10 — CRC32 header; async save; `AgentState` + `NpcMemoryComponent` inline per NPC record; `WorldSimulation` faction/trade tail
- **`SaveVersionChain`** — O3DE-inspired post-load version converter chain (`Register(from,to,fn)` + `RunUpgrades`); eliminates growing `is_vN` branches for future versions
- **`MdStatusRegistry`** — Flare CampaignManager-inspired string-based status flags; `GetAllCSV`/`SetAllCSV` for human-readable saves; FNV-1a hashed IDs; MAX=128
- **`MdPrefabRegistry`** — data-driven NPC archetypes via `data/prefabs/prefabs.json`; `MdPrefab` {name, bt\_template, hp, wander\_radius, combat\_profile}; MAX=32

### Config & Features
- **`MdIniReader`** — zelda3-inspired INI parser; sections, `key=value`, ParseBool (0/1/yes/no/true/false/on/off), `!include`, `monkey_dust.user.ini` override fallback
- **`MdFeature`** uint32 bitmask — 8 accessibility toggles (DisableLowHealthBeep, SkipIntroOnKeypress, ShowFps, etc.); `MdFeaturesLoad(ini)` from `[Features]` INI section
- **`MdModuleRegistry`** — O3DE Gem-inspired plug-in lifecycle for `tools/` targets; `Register/Load/Unload/UnloadAll` (reverse order); `data/modules/*.module.json` metadata

### GPU Dev Tooling
- **`GpuPipeline::Reload()`** — invalidates SPV cache entry + destroys + re-reads SPIR-V from disk; safe to call mid-session for hot-reload
- **`MdSpvCache_Invalidate(path)`** — evicts one SPIR-V cache entry by glsl path (e.g. `"shaders/char_hair.frag"`)
- **`MdSpvCache_Shutdown()`** — releases all cached bytecode at exit
- `SkinMesh::LoadGLB` validates NORMAL / JOINTS / WEIGHTS attributes and emits `[SkinMesh] WARN` if missing (missing normals → `normalize(0)` → NaN → white fragments)
- Hair shader (`char_hair.vert/.frag`) — Lambert only (Kajiya-Kay removed: V≈−L → normalize(0) → NaN); depth bias `gl_Position.z -= 0.0003*w` prevents Z-fighting against head geometry

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
```

> **Shaders** live in the parent [`monkey_dust`](https://github.com/rdga1bot/monkey_dust) game repository (`shaders/` + `scripts/compile_shaders.sh`).
> The engine library itself is shader-agnostic — it loads pre-compiled SPIR-V at runtime via `GpuPipeline::Create(desc)`.

**Dependencies** (bring your own or via CMake FetchContent):
`SDL3` · `EnTT` · `Recast/Detour` · `ozz-animation` · `JoltPhysics` · `miniaudio` · `Lua 5.4`

> **ImGui is NOT an engine dependency.** Dear ImGui and all extensions (imnodes, imgui-node-editor, ImGuiColorTextEdit, imguizmo, imgui-flame-graph, imgui-command-palette) live in `tools/third_party/`. The engine library has zero UI dependencies and is split-ready.

---

## Tests

```bash
ninja -C build md_tests
./build/tests/md_tests          # 1557 tests across 206+ suites
```

Suites: FNV · AgentBlackboard · FlowGraph · DirectorSystem · PowerSlotManager · NpcConfig · HotReload · FlowVar · AI Patterns C1–C20 · BT VM · Batch 3–31 · M47–M59 (NavLod, ReplaySnapshot, AllianceMatrix, SenseSystem, BT Archetypes, CombatDispatch, DeferredLighting, DialogQuest, FootIK) · O3DE-1–4 (MaterialDesc, MdPrefabRegistry, SaveVersionChain, MdModuleRegistry) · ZLD-1–2 (MdIniReader, MdFeature) · FL-3–4 (MdStatusRegistry, MdEventScheduler) · KEN-1–8 (SkinMesh, OzzAnimPlayer, JoltWorld, CrowdSystem) · VBfA-R1–9 · VBfA-AI1–6

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
    platform/              ← input/audio/window/md_fs/md_log/md_hints/timing_system
    render/                ← GPU HAL, ring buffer, shadow, SSAO, SMAA …
    save/                  ← SaveSystem v10 · SaveVersionChain
    scripting/             ← LuaSystem, LuaEventBus, FlowGraph
    world/                 ← FactionSystem, WorldSimulation, TransformSoA, SettlementPlacer, MdStatusRegistry, MdPrefabRegistry …
    prefab/                ← MdPrefabRegistry (data-driven NPC archetypes)
    module/                ← MdModuleRegistry (plug-in lifecycle)
    asset/                 ← (reserved: MdAssetHandle when asset pipeline scales)
  src/                     ← implementation units
  tests/                   ← Google Test suite
```

---

## License

MIT — see [LICENSE](LICENSE).

The game itself (`monkey_dust` executable and `game/` sources) is proprietary and not part of this repository.
