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
