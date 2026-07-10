#include <monkey_dust/world/clutter_gen.h>

// ClutterGen_Upload lives in a separate TU so worker-thread-only callers of
// ClutterGen_Build do NOT pull in GpuStaticBuffer::Init (mirrors terrain_upload.cpp).

static void s_upload_core(TerrainChunk& chunk,
                          const PropVertex* verts, int vert_count,
                          const uint16_t*   idx,   int idx_count)
{
    // Batched (see GpuUploadBatch doc comment, gpu_hal.h) — one transfer buffer
    // + one submit for both buffers instead of two separate ones per chunk.
    uint32_t vbytes = sizeof(PropVertex) * (uint32_t)vert_count;
    uint32_t ibytes = sizeof(uint16_t)   * (uint32_t)idx_count;
    GpuUploadBatch batch;
    batch.Begin(vbytes + ibytes);
    batch.Add(chunk.clutter_vbo, 0x8892u, verts, vbytes);
    batch.Add(chunk.clutter_ibo, 0x8893u, idx,   ibytes);
    batch.End();
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
