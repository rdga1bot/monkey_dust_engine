#pragma once
#include <cstdint>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/nav/navmesh.h>
#include <monkey_dust/world/chunk_def.h>
#include <monkey_dust/world/terrain_pass_grid.h>

// Geomorphing: blends vertex Y toward coarser-LOD target beyond 420m (ramp 420–600m).
// morph_y filled by TerrainGen_Build; consumed by terrain_pom.vert.
//
// NOTE: this flag is a SEPARATE preprocessor from shaders/terrain_pom.vert's
// own `TERRAIN_GEOMORPH_ENABLED` (C++ vs GLSL, no shared preprocessor —
// confirmed by independent audit, docs/ENGINE_AUDIT.md/TERRAIN_FIX_PROMPT.md).
// This one only gates whether morph_y is COMPUTED on the CPU (currently
// always on); the shader's flag independently gates whether the GPU
// actually USES it (currently off). Flipping this one alone does nothing
// visually — see shaders/terrain_pom.vert:9's matching comment.
#define TERRAIN_MORPH_DATA_ENABLED 1

// Terrain grid: 128×128 quads = 129×129 vertices per chunk (CHUNK_SIZE=460.8m
// -> 3.6m/quad). Matches Kenshi's own real in-engine resolution (RE-confirmed:
// raw 258x258 tile fetch downsampled internally to 129x129/zone,
// re_docs/kenshi/terrain.md) — was 64/65 (coarser than Kenshi itself).
static constexpr int   TERRAIN_GRID  = 128;
static constexpr int   TERRAIN_VERTS = (TERRAIN_GRID + 1) * (TERRAIN_GRID + 1); // 16641
static constexpr int   TERRAIN_TRIS  = TERRAIN_GRID * TERRAIN_GRID * 2;         // 32768
static constexpr int   TERRAIN_IDX   = TERRAIN_TRIS * 3;                         // 98304
static constexpr float TERRAIN_STEP  = CHUNK_SIZE / TERRAIN_GRID;               // 3.6m

// LOD levels (same VBO, coarser IBO — steps of 2/4/8 vertices)
static constexpr int   TERRAIN_LOD_LEVELS    = 3;
static constexpr int   TERRAIN_LOD_STEPS[3]  = { 2, 4, 8 };
static constexpr int   TERRAIN_LOD_IDX[3]    = {
    (TERRAIN_GRID/2) * (TERRAIN_GRID/2) * 6,  // 24576 (64×64 quads)
    (TERRAIN_GRID/4) * (TERRAIN_GRID/4) * 6,  //  6144 (32×32 quads)
    (TERRAIN_GRID/8) * (TERRAIN_GRID/8) * 6   //  1536 (16×16 quads)
};
// Camera-to-chunk-centre distance thresholds to switch LOD levels
static constexpr float TERRAIN_LOD_DIST[3]   = { 600.f, 1200.f, 2000.f };

// Vertex layout (stride = 48 bytes):
//   location 0: vec3 pos    (12)
//   location 1: vec3 normal (12)
//   location 2: vec2 uv     (8)   — world-space UV for texture tiling
//   location 3: vec4 splat  (16)  — float weights: [grass, rock, dirt, snow]
struct TerrainVertex {
    float x, y, z;           // world position        offset  0
    float nx, ny, nz;         // normal                offset 12
    float u, v;               // UV (world-space)      offset 24
    float splat[4];           // [grass,rock,dirt,snow] offset 32
    float morph_y;            // geomorph target Y for L0→L1; guarded by TERRAIN_MORPH_DATA_ENABLED
};
static_assert(sizeof(TerrainVertex) == 52, "TerrainVertex size mismatch");

// Raw heightmap data — stored separately for CPU-side queries (NPC grounding, etc.)
struct TerrainHeightmap {
    float h[TERRAIN_VERTS];   // [row * (TERRAIN_GRID+1) + col]
};

// ── Per-chunk procedural props ────────────────────────────────────────────────
// Props are generated during PropGen_Build (game-side) and stored per-chunk.
// They stream in/out automatically with terrain chunk loading.
enum ChunkPropType : uint8_t {
    kPropRock      = 0,  // rock_01       (~2 m)
    kPropFormation = 1,  // rock_formation (~2 m)
    kPropHatRock   = 2,  // hat_rock       (~5 m, dramatic, rare)
    kPropYucca     = 3,  // yucca          (~1.7 m)
    kPropCanyonRock= 4,  // canyon_rock    (~1.3 m, small stony plant)
    kPropDeadTree  = 5,  // dead_tree      (~1.8 m)
    kPropBones     = 6,  // bones          (ribcage, ~1.5 m, wasteland dressing)
    kPropRuinJunk  = 7,  // ruin_junk      (concrete rubble chunk, ~1 m)
    kPropSpike     = 8,  // spike_bloom    (hazard plant/spike cluster, ~2 m, rare — heavy mesh)
    kPropTypeCount = 9
};

struct ChunkPropInstance {
    float   x, y, z;    // world-space position (y already accounts for embed)
    float   nx, ny, nz; // terrain surface normal (sampled by PropGen_Build, G-2)
    uint8_t type;        // ChunkPropType
    uint8_t _pad[3];
};
static_assert(sizeof(ChunkPropInstance) == 28, "ChunkPropInstance size");
static constexpr int CHUNK_MAX_PROPS = 64;

// Skirt geometry constants: 4 edges × (TERRAIN_GRID+1) × 2 verts (top+bottom)
static constexpr int TERRAIN_SKIRT_VERTS = 4 * (TERRAIN_GRID + 1) * 2;  // 520
static constexpr int TERRAIN_SKIRT_IDX   = 4 *  TERRAIN_GRID * 6;       // 1536

struct TerrainChunk {
    ChunkCoord      coord;
    float           center_x = 0.f;    // world-space centre (set by TerrainGen_Build)
    float           center_z = 0.f;
    GpuStaticBuffer vbo;               // TerrainVertex * TERRAIN_VERTS
    GpuStaticBuffer ibo;               // uint16_t * TERRAIN_IDX  (L0: 128×128)
    GpuStaticBuffer ibo_lod[3];        // L1: 64×64, L2: 32×32, L3: 16×16
    GpuStaticBuffer skirt_vbo;         // TerrainVertex * TERRAIN_SKIRT_VERTS (Item 7)
    GpuStaticBuffer skirt_ibo;         // uint16_t * TERRAIN_SKIRT_IDX
    NavMesh          navmesh;
    TerrainHeightmap heightmap;         // CPU copy for height queries
    TerrainPassGrid  pass_grid;         // L2-inspired O(1) passability bitmask
    ChunkPropInstance props[CHUNK_MAX_PROPS];
    int              prop_count = 0;    // valid entries in props[]
    // GroundTexLayer indices: 0=base,1=slope,2=cliff (per biome), 3=grass,4=dirt,5=road (global)
    float            ground_layers[6] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f};
    // Procedural biome crossfade targets: xyz=slot A (base/slope/cliff of the
    // FIRST grid-adjacent neighbour chunk whose resolved biome differs),
    // w=slot B (base index only of the SECOND differing neighbour, for
    // chunks at a 3-way zone corner -- one blended boundary isn't enough
    // there, confirmed visually). Equal to ground_layers[0]/this chunk's own
    // base when no such neighbour exists -- blend weight is then always 0
    // regardless of the mask texture, so a stray nonzero sample still blends
    // into itself (a no-op) rather than showing a wrong texture. See
    // ARCHITECTURE note in terrain_gen.cpp's biome-assignment block for the
    // real-Kenshi (blendmap.png/blendinfo.dat) precedent this mirrors.
    float            blend_layers[4] = {0.f, 1.f, 2.f, 0.f};
    bool             loaded = false;
    // Set true at the end of TerrainGen_Build — independent of `loaded` (which
    // means "GPU buffers uploaded", set by TerrainGen_Upload). Lets callers
    // that only need CPU heightmap data (e.g. the editor's full-world compact
    // VBO bake) work correctly even for chunks that were never GPU-uploaded —
    // see editor_world_3d_sdlgpu.cpp's windowed lazy-upload (GpuUploadBatch
    // doc comment, gpu_hal.h, explains why eager per-chunk GPU upload for all
    // 4096 chunks is not viable).
    bool             heightmap_ready = false;

    // KEN-CLUTTER Tier 2: dense ground clutter (pebbles/small rocks/small plants)
    // baked into ONE static mesh per chunk by ClutterGen_Build/Upload — one draw
    // call regardless of instance count (mirrors Kenshi's Forests::BatchedGeometry).
    GpuStaticBuffer  clutter_vbo;
    GpuStaticBuffer  clutter_ibo;
    int              clutter_index_count = 0;
    bool             clutter_loaded = false;

    // Terrain surface construction rethink (2026-07-12): the 6-9 sample
    // ground-texture blend (BlendGroundLayers) is baked ONCE per chunk into
    // this small offscreen texture instead of recomputed every fragment
    // every frame (TerrainRenderer::BakeAlbedo, called once right after
    // upload — same one-shot-per-chunk lifecycle as clutter_vbo/loaded
    // above). Runtime terrain_forward/terrain_pom shaders sample it once
    // instead of doing the full blend. See docs/OSS_TERRAIN_METHODS.md's
    // Wicked Engine section for the reference pattern this mirrors.
    GpuTexture       albedo_tex;
    bool             albedo_baked = false;

    // Sample height at local chunk coords (0..CHUNK_SIZE).
    // Bilinear interpolation between grid cells.
    float SampleHeight(float lx, float lz) const;
};

// Per-chunk terrain generation parameters
struct TerrainGenParams {
    float base_scale    = 0.008f;  // noise frequency (lower = larger features)
    float amplitude     = 18.0f;   // max height in metres
    int   octaves       = 6;
    float persistence   = 0.50f;
    float lacunarity    = 2.00f;
    float sea_level     = 0.0f;    // heights below this → flatten to 0
    int   seed          = 42;
    // NavMesh Recast parameters — larger cs = faster but lower quality
    float nav_cs        = 0.5f;    // cell size (m); 0.3=precise, 1.0=fast/test
    float nav_ch        = 0.2f;    // cell height (m)
    // World-space origin offset — shifts all vertex positions without moving noise.
    float world_offset_x = 0.f;
    float world_offset_z = 0.f;
    // Optional Kenshi heightmap file (.r32).  When set, noise is replaced by
    // sampled heights from the file.  amplitude still scales the result.
    const char* heightmap_r32 = nullptr;

    // Per-zone Kenshi heightmaps (overrides heightmap_r32 when zone_origin_x >= 0).
    // Each chunk (coord.x, coord.z) loads zone_{zone_origin_x+coord.x}_{zone_origin_z+coord.z}.r32.
    // Heights are stored in metres; amplitude is ignored.
    int zone_origin_x = -1;   // Kenshi zone X for chunk coord(0,0)
    int zone_origin_z = -1;   // Kenshi zone Z for chunk coord(0,0)

    // Procedural open-world parameters (defaults = disabled, preserves existing behaviour).
    float domain_warp_strength = 0.f;   // 0=disabled; 80.f for open world
    float ridge_weight         = 0.f;   // 0=disabled; 0.12f for open world
    float redistribution_power = 1.f;   // 1=no change; 3.0f for open world
    bool  force_noise          = false; // true = ignore atlas/r32, always use noise

    // Biome + faction (PROC-2). biome_scale controls region size in world-space.
    float biome_scale   = 0.f;   // 0=disabled; 0.00025f for open world (~4km regions)
    float faction_scale = 0.f;   // unused at gen time; kept for future query API
    int   num_factions  = 0;     // 0=disabled; 8 for open world

    // Targets world_hmap.r32 stats: avg≈17m, σ≈22m, 95.9% below 60m, max≈280m.
    static TerrainGenParams ProcWorld(int seed = 42) {
        TerrainGenParams p;
        p.seed                 = seed;
        p.base_scale           = 0.004f;   // 250m per wavelength — Kenshi dune scale
        p.amplitude            = 280.f;    // matches Kenshi max ~280m
        p.octaves              = 6;
        p.persistence          = 0.50f;
        p.lacunarity           = 2.00f;
        p.domain_warp_strength = 15.f;
        p.ridge_weight         = 0.08f;
        p.redistribution_power = 6.5f;
        p.force_noise          = true;
        p.biome_scale          = 0.00025f; // ~4km biome regions
        p.num_factions         = 8;
        return p;
    }
};
