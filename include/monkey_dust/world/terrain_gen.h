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
const TerrainVertex* TerrainGen_StagedSkirtVerts();   // Item 7: skirt geometry
const uint16_t*      TerrainGen_StagedSkirtIndices();

// Async upload path (Item 5): upload from caller-provided buffers instead of
// the shared static staging buffers (avoids race when worker thread reuses staging).
void TerrainGen_UploadFrom(TerrainChunk& chunk,
                           const TerrainVertex* verts,
                           const uint16_t*      idx,
                           const TerrainVertex* skirt_v,
                           const uint16_t*      skirt_i);

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
// Smooth atlas heights at every zone boundary (3-vertex kernel) to eliminate
// Kenshi fullmap zone-seam cliffs that create NdotL=0 faces at chunk edges.
// Call once after Load, before any TerrainGen_Build.
void  TerrainAtlas_SmoothBoundaries();
// Stitch one boundary seam between adjacent zones after rolling-shift loads a new edge.
// dir=0: X-seam between (zx,zy) and (zx+1,zy). dir=1: Z-seam (zx,zy) and (zx,zy+1).
// No-op if already smoothed (idempotent — re-smoothing a smooth boundary is harmless).
void  TerrainAtlas_StitchEdge(int zx, int zy, int dir);
// Get/Set individual vertex height (zx,zy in 0..63; col,row in 0..64)
float TerrainAtlas_GetHeight(int zx, int zy, int col, int row);
void  TerrainAtlas_SetHeight(int zx, int zy, int col, int row, float h);
// Save only dirty zones back to the atlas file (fast partial update)
bool  TerrainAtlas_Save(const char* path);
bool  TerrainAtlas_ZoneDirty(int zx, int zy);

// Delta-edit layer (on top of read-only TIF mmap).
// SaveEdits writes only edited zones; LoadEdits applies them at startup.
bool  TerrainAtlas_SaveEdits(const char* path);
bool  TerrainAtlas_LoadEdits(const char* path);
bool  TerrainAtlas_HasEdits();

// ── Master heightmap API ─────────────────────────────────────────────────────
// Low-resolution macro geography layer blended under procedural noise.
// world_extent_x/z: total world size in metres the heightmap covers.
// For a 64-zone atlas: world_extent = 64 * CHUNK_SIZE = 294912m.
bool  TerrainMaster_Load(const char* path, float world_extent_x, float world_extent_z);
bool  TerrainMaster_Loaded();
// World-space height sample (wx,wz in metres; bilinear)
float TerrainMaster_SampleWorld(float wx, float wz);
// Grid pixel access (col in [0,W-1], row in [0,H-1])
float TerrainMaster_GetPixel(int col, int row);
void  TerrainMaster_SetPixel(int col, int row, float h);
int   TerrainMaster_Width();
int   TerrainMaster_Height();
float TerrainMaster_HMax();
// Overwrite file with current pixel data
bool  TerrainMaster_Save(const char* path);
