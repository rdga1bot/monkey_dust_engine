#include <monkey_dust/world/terrain_gen.h>

// TerrainGen_Upload lives in a separate TU so test binaries that call
// TerrainGen_Build do NOT pull in GpuStaticBuffer::Init (and glad symbols).
void TerrainGen_Upload(TerrainChunk& chunk) {
    chunk.vbo.Init(0x8892u /*GL_ARRAY_BUFFER*/,
                   TerrainGen_StagedVerts(),
                   sizeof(TerrainVertex) * TERRAIN_VERTS);
    chunk.ibo.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/,
                   TerrainGen_StagedIndices(),
                   sizeof(uint16_t) * TERRAIN_IDX);
    chunk.loaded = true;
}
