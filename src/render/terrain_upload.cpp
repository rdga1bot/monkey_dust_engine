#include <monkey_dust/world/terrain_gen.h>

// TerrainGen_Upload lives in a separate TU so test binaries that call
// TerrainGen_Build do NOT pull in GpuStaticBuffer::Init (and glad symbols).

static void build_lod_ibo(uint16_t* out, int step)
{
    const int G = TERRAIN_GRID / step;
    const int S = TERRAIN_GRID + 1;  // vertex stride per row
    int ii = 0;
    for (int row = 0; row < G; ++row) {
        for (int col = 0; col < G; ++col) {
            uint16_t bl = (uint16_t)( row      * step * S + col * step);
            uint16_t br = (uint16_t)( row      * step * S + col * step + step);
            uint16_t tl = (uint16_t)((row + 1) * step * S + col * step);
            uint16_t tr = (uint16_t)((row + 1) * step * S + col * step + step);
            out[ii++] = bl; out[ii++] = br; out[ii++] = tl;
            out[ii++] = br; out[ii++] = tr; out[ii++] = tl;
        }
    }
}

static void s_upload_core(TerrainChunk& chunk,
                          const TerrainVertex* verts,
                          const uint16_t*      idx,
                          const TerrainVertex* skirt_v,
                          const uint16_t*      skirt_i)
{
    // Batched: 7 buffers (vbo+ibo+3×ibo_lod+skirt_vbo+skirt_ibo) in ONE transfer
    // buffer + ONE command buffer submit, not 7 separate ones — see GpuUploadBatch
    // doc comment (gpu_hal.h) for why unbatched per-chunk uploads crash the
    // editor's full 64×64 world load around chunk ~4000 (Intel ANV driver
    // command-buffer churn corrupts its own heap state).
    static uint16_t lod_tmp[TERRAIN_LOD_LEVELS][TERRAIN_LOD_IDX[0]];
    for (int li = 0; li < TERRAIN_LOD_LEVELS; ++li)
        build_lod_ibo(lod_tmp[li], TERRAIN_LOD_STEPS[li]);

    uint32_t total = sizeof(TerrainVertex) * TERRAIN_VERTS
                   + sizeof(uint16_t)      * TERRAIN_IDX
                   + sizeof(TerrainVertex) * TERRAIN_SKIRT_VERTS
                   + sizeof(uint16_t)      * TERRAIN_SKIRT_IDX;
    for (int li = 0; li < TERRAIN_LOD_LEVELS; ++li)
        total += sizeof(uint16_t) * TERRAIN_LOD_IDX[li];

    GpuUploadBatch batch;
    batch.Begin(total);
    batch.Add(chunk.vbo, 0x8892u, verts, sizeof(TerrainVertex) * TERRAIN_VERTS);
    batch.Add(chunk.ibo, 0x8893u, idx,   sizeof(uint16_t)      * TERRAIN_IDX);
    for (int li = 0; li < TERRAIN_LOD_LEVELS; ++li)
        batch.Add(chunk.ibo_lod[li], 0x8893u, lod_tmp[li], sizeof(uint16_t) * TERRAIN_LOD_IDX[li]);
    // Item 7: skirt geometry upload
    batch.Add(chunk.skirt_vbo, 0x8892u, skirt_v, sizeof(TerrainVertex) * TERRAIN_SKIRT_VERTS);
    batch.Add(chunk.skirt_ibo, 0x8893u, skirt_i, sizeof(uint16_t)      * TERRAIN_SKIRT_IDX);
    batch.End();

    chunk.loaded = true;
}

void TerrainGen_Upload(TerrainChunk& chunk)
{
    s_upload_core(chunk,
                  TerrainGen_StagedVerts(),
                  TerrainGen_StagedIndices(),
                  TerrainGen_StagedSkirtVerts(),
                  TerrainGen_StagedSkirtIndices());
}

// Item 5: async upload — caller provides per-slot copies of staging data
void TerrainGen_UploadFrom(TerrainChunk& chunk,
                           const TerrainVertex* verts,
                           const uint16_t*      idx,
                           const TerrainVertex* skirt_v,
                           const uint16_t*      skirt_i)
{
    s_upload_core(chunk, verts, idx, skirt_v, skirt_i);
}
