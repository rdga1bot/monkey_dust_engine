#pragma once
#include <cstdint>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/nav/navmesh.h>
#include <monkey_dust/world/chunk_def.h>

// LOD IBO disabled in RD-4; always L0 64×64. Set to 1 to re-enable geomorphing.
#define TERRAIN_GEOMORPH_ENABLED 0

// Terrain grid: 64×64 quads = 65×65 vertices per chunk (CHUNK_SIZE=500m → 7.8m/quad)
static constexpr int   TERRAIN_GRID  = 64;
static constexpr int   TERRAIN_VERTS = (TERRAIN_GRID + 1) * (TERRAIN_GRID + 1); // 4225
static constexpr int   TERRAIN_TRIS  = TERRAIN_GRID * TERRAIN_GRID * 2;         // 8192
static constexpr int   TERRAIN_IDX   = TERRAIN_TRIS * 3;                         // 24576
static constexpr float TERRAIN_STEP  = CHUNK_SIZE / TERRAIN_GRID;               // 7.8m

// LOD levels (same VBO, coarser IBO — steps of 2/4/8 vertices)
static constexpr int   TERRAIN_LOD_LEVELS    = 3;
static constexpr int   TERRAIN_LOD_STEPS[3]  = { 2, 4, 8 };
static constexpr int   TERRAIN_LOD_IDX[3]    = {
    (TERRAIN_GRID/2) * (TERRAIN_GRID/2) * 6,  // 6144  (32×32 quads)
    (TERRAIN_GRID/4) * (TERRAIN_GRID/4) * 6,  // 1536  (16×16 quads)
    (TERRAIN_GRID/8) * (TERRAIN_GRID/8) * 6   //  384  (8×8 quads)
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
    float morph_y;            // geomorph target Y for L0→L1; guarded by TERRAIN_GEOMORPH_ENABLED
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
    kPropTypeCount = 6
};

struct ChunkPropInstance {
    float   x, y, z;   // world-space position (y already accounts for embed)
    uint8_t type;       // ChunkPropType
    uint8_t _pad[3];
};
static_assert(sizeof(ChunkPropInstance) == 16, "ChunkPropInstance size");
static constexpr int CHUNK_MAX_PROPS = 64;

struct TerrainChunk {
    ChunkCoord      coord;
    float           center_x = 0.f;    // world-space centre (set by TerrainGen_Build)
    float           center_z = 0.f;
    GpuStaticBuffer vbo;               // TerrainVertex * TERRAIN_VERTS
    GpuStaticBuffer ibo;               // uint16_t * TERRAIN_IDX  (L0: 64×64)
    GpuStaticBuffer ibo_lod[3];        // L1: 32×32, L2: 16×16, L3: 8×8
    NavMesh         navmesh;
    TerrainHeightmap heightmap;        // CPU copy for height queries
    ChunkPropInstance props[CHUNK_MAX_PROPS];
    int             prop_count = 0;    // valid entries in props[]
    bool            loaded = false;

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
};
