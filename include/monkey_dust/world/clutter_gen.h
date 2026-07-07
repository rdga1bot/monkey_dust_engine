#pragma once
#include <monkey_dust/world/terrain_chunk.h>
#include <monkey_dust/render/prop_mesh.h>  // reuse PropVertex {pos,normal} layout
#include <cstdint>

// ── KEN-CLUTTER Tier 2: dense ground clutter ──────────────────────────────────
// Real Kenshi (RE-confirmed: kenshi_economy.jsonl block 01359, Forests::BatchedGeometry)
// never draws pebbles/small rocks/small plants as one-draw-call-per-object. It bakes
// many small mesh instances into ONE static vertex/index buffer per page (≈ our
// chunk), so GPU cost is a single draw call regardless of instance count.
//
// This mirrors that: at chunk-build time (worker thread), copy+transform each
// placed instance's source-mesh vertices (already world-space) into a shared
// staging buffer, merging multiple small meshes with index-offset rebasing.
// No GPU calls happen here — see clutter_upload.cpp for the main-thread upload,
// split into its own TU exactly like terrain_gen.cpp / terrain_upload.cpp so
// CPU-only callers (worker thread) don't pull in GpuStaticBuffer::Init.

// Budget sized to stay under the uint16 index limit (65535 verts) with margin —
// see prop_gen.cpp-style biome density for how this scales; start conservative,
// profile on Intel HD 520 before raising (CLAUDE.md: measure, don't assume).
static constexpr int CLUTTER_MAX_VERTS = 40000;
static constexpr int CLUTTER_MAX_IDX   = 140000;

// Call once at startup (main thread, before TerrainStreamQueue::Get().start()) —
// loads the small CPU-only source meshes used to build clutter. No GPU calls.
bool ClutterGen_LoadSources();

// Worker thread: bake this chunk's clutter into the shared static staging
// buffers. No GPU calls. Valid only until the next ClutterGen_Build call.
void ClutterGen_Build(TerrainChunk& chunk, const char* biome_slug);

const PropVertex* ClutterGen_StagedVerts();
const uint16_t*   ClutterGen_StagedIndices();
int               ClutterGen_StagedVertCount();
int               ClutterGen_StagedIndexCount();

// Main thread: upload staged (or caller-supplied, for the async queue) buffers
// to GPU. Implemented in clutter_upload.cpp (GPU-aware TU).
void ClutterGen_Upload(TerrainChunk& chunk);
void ClutterGen_UploadFrom(TerrainChunk& chunk,
                           const PropVertex* verts, int vert_count,
                           const uint16_t* idx, int idx_count);
