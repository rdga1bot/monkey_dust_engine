#include <monkey_dust/world/clutter_gen.h>

// ClutterGen_Upload lives in a separate TU so worker-thread-only callers of
// ClutterGen_Build do NOT pull in GpuStaticBuffer::Init (mirrors terrain_upload.cpp).

static void s_upload_core(TerrainChunk& chunk,
                          const PropVertex* verts, int vert_count,
                          const uint16_t*   idx,   int idx_count)
{
    chunk.clutter_vbo.Init(0x8892u, verts, sizeof(PropVertex) * (uint32_t)vert_count);
    chunk.clutter_ibo.Init(0x8893u, idx,   sizeof(uint16_t)   * (uint32_t)idx_count);
    chunk.clutter_index_count = idx_count;
    chunk.clutter_loaded      = true;
}

void ClutterGen_Upload(TerrainChunk& chunk)
{
    s_upload_core(chunk,
                  ClutterGen_StagedVerts(),   ClutterGen_StagedVertCount(),
                  ClutterGen_StagedIndices(), ClutterGen_StagedIndexCount());
}

void ClutterGen_UploadFrom(TerrainChunk& chunk,
                           const PropVertex* verts, int vert_count,
                           const uint16_t*   idx,   int idx_count)
{
    s_upload_core(chunk, verts, vert_count, idx, idx_count);
}
