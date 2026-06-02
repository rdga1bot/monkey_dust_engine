#include <monkey_dust/world/terrain_gen.h>

// TerrainGen_Upload lives in a separate TU so test binaries that call
// TerrainGen_Build do NOT pull in GpuStaticBuffer::Init (and glad symbols).

// Temporary buffer for LOD IBO generation (largest LOD1: 32×32×6 = 6144 indices)
static uint16_t s_lod_tmp[6144];

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
    chunk.vbo.Init(0x8892u, verts,   sizeof(TerrainVertex) * TERRAIN_VERTS);
    chunk.ibo.Init(0x8893u, idx,     sizeof(uint16_t)      * TERRAIN_IDX);

    for (int li = 0; li < TERRAIN_LOD_LEVELS; ++li) {
        build_lod_ibo(s_lod_tmp, TERRAIN_LOD_STEPS[li]);
        chunk.ibo_lod[li].Init(0x8893u, s_lod_tmp,
                               sizeof(uint16_t) * TERRAIN_LOD_IDX[li]);
    }

    // Item 7: skirt geometry upload
    chunk.skirt_vbo.Init(0x8892u, skirt_v, sizeof(TerrainVertex) * TERRAIN_SKIRT_VERTS);
    chunk.skirt_ibo.Init(0x8893u, skirt_i, sizeof(uint16_t)      * TERRAIN_SKIRT_IDX);

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
