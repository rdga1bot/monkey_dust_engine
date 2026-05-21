#pragma once
#include <monkey_dust/world/terrain_chunk.h>

// Generates a TerrainChunk (mesh + heightmap + navmesh) from params.
// Call on a worker thread; GpuStaticBuffer::Init() must be deferred to main thread.
// Returns false on allocation failure (navmesh OOM).
bool TerrainGen_Build(TerrainChunk& out, ChunkCoord coord, const TerrainGenParams& p);

// Upload VBO/IBO to GPU. Call from main (render) thread after TerrainGen_Build.
// Implemented in engine/src/render/terrain_upload.cpp (GPU-aware TU, not linked
// by test binaries that only call TerrainGen_Build).
void TerrainGen_Upload(TerrainChunk& chunk);

// Staging buffer accessors (filled by TerrainGen_Build, consumed by TerrainGen_Upload).
// Valid only until the next TerrainGen_Build call.
const TerrainVertex* TerrainGen_StagedVerts();
const uint16_t*      TerrainGen_StagedIndices();

// Simplex noise — public domain (Stefan Gustavson / Ashima Arts).
// grad4[] and permutation tables are file-internal; do not expose.
float SimplexNoise2(float x, float y);

// Fractal Brownian Motion over SimplexNoise2.
float FBM2(float x, float y, int octaves, float persistence, float lacunarity);

// ── Terrain Atlas API ────────────────────────────────────────────────────────
// world_hmap.r32: one 66 MB file replacing 4096 zone_X_Y.r32 files.
// Atlas magic: 0x414D4800. Layout: header(16B) + 4096 × zone_block(16908B).
// zone_block: float hmin + float hmax + float[65*65] heights (row=Z, col=X).

bool  TerrainAtlas_Load(const char* path);  // load into static RAM buffer
bool  TerrainAtlas_Loaded();                // true after successful Load
// Get/Set individual vertex height (zx,zy in 0..63; col,row in 0..64)
float TerrainAtlas_GetHeight(int zx, int zy, int col, int row);
void  TerrainAtlas_SetHeight(int zx, int zy, int col, int row, float h);
// Save only dirty zones back to the atlas file (fast partial update)
bool  TerrainAtlas_Save(const char* path);
bool  TerrainAtlas_ZoneDirty(int zx, int zy);
