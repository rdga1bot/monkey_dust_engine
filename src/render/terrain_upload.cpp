#include <monkey_dust/world/terrain_gen.h>

// task terrain-dedup (2026-07-29): TerrainGen_Upload/UploadFrom used to
// batch-upload 7 GPU buffers per chunk (vbo/ibo/3×ibo_lod/skirt_vbo/
// skirt_ibo + steepness_ssbo) — all now REMOVED from TerrainChunk, verified
// zero readers (TerrainPatchRenderer/Granite is the sole active renderer,
// reads its own world-wide heightmap texture, never per-chunk GPU mesh
// data). See terrain_renderer.h's class doc comment and terrain_gen.h's
// updated TerrainGen_Upload comment for the full investigation. All that's
// left of "upload" is the loaded=true handoff signal other systems
// (TerrainQuery, physics readiness checks) already gate on.
//
// Kept as a real (non-inline) function in this same, separate GPU-aware TU
// — even though it no longer touches the GPU at all — purely so the
// dozens of existing TerrainGen_Build+TerrainGen_Upload call-site pairs
// across game/tools don't need touching, and so a future per-chunk GPU
// resource has an obvious place to attach without re-threading every caller.

void TerrainGen_Upload(TerrainChunk& chunk)
{
    chunk.loaded = true;
}

void TerrainGen_UploadFrom(TerrainChunk& chunk,
                           const TerrainVertex* /*verts*/,
                           const uint16_t*      /*idx*/,
                           const TerrainVertex* /*skirt_v*/,
                           const uint16_t*      /*skirt_i*/)
{
    chunk.loaded = true;
}
