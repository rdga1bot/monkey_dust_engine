#pragma once
#include <cstdint>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/nav/navmesh.h>
#include <monkey_dust/world/chunk_def.h>

// Terrain grid: 64×64 quads = 65×65 vertices per chunk (CHUNK_SIZE=64m → 1m/quad)
static constexpr int   TERRAIN_GRID  = 64;
static constexpr int   TERRAIN_VERTS = (TERRAIN_GRID + 1) * (TERRAIN_GRID + 1); // 4225
static constexpr int   TERRAIN_TRIS  = TERRAIN_GRID * TERRAIN_GRID * 2;         // 8192
static constexpr int   TERRAIN_IDX   = TERRAIN_TRIS * 3;                         // 24576
static constexpr float TERRAIN_STEP  = CHUNK_SIZE / TERRAIN_GRID;               // 1.0m

// Vertex layout (stride = 48 bytes):
//   location 0: vec3 pos    (12)
//   location 1: vec3 normal (12)
//   location 2: vec2 uv     (8)   — world-space UV for texture tiling
//   location 3: vec4 splat  (16)  — float weights: [grass, rock, dirt, snow]
struct TerrainVertex {
    float x, y, z;           // world position
    float nx, ny, nz;         // normal
    float u, v;               // UV (world-space, for tiling)
    float splat[4];           // [grass, rock, dirt, snow], sum=1
};
static_assert(sizeof(TerrainVertex) == 48, "TerrainVertex size mismatch");

// Raw heightmap data — stored separately for CPU-side queries (NPC grounding, etc.)
struct TerrainHeightmap {
    float h[TERRAIN_VERTS];   // [row * (TERRAIN_GRID+1) + col]
};

struct TerrainChunk {
    ChunkCoord      coord;
    GpuStaticBuffer vbo;       // TerrainVertex * TERRAIN_VERTS
    GpuStaticBuffer ibo;       // uint16_t * TERRAIN_IDX
    NavMesh         navmesh;
    TerrainHeightmap heightmap; // CPU copy for height queries
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
};
