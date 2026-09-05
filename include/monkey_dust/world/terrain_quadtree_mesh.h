#pragma once
#include <cstdint>
#include <vector>

// 2026-09-05 (docs/TERRAIN_FLAT_LOD_PLAN.md): a SHARED, tiny index buffer
// describing ONE tile's topology -- patchSize=16 (16x16 quads, 17x17 grid
// positions), matching real Kenshi's own "standard" Ogre::Terrain patch
// size exactly (re_docs/kenshi/terrain.md Subsystem 5). Every quadtree tile,
// everywhere in the world, reuses this SAME index buffer (only a per-tile
// origin/texelSize UBO push differs) -- there is deliberately NO per-tile
// vertex buffer: TerrainQuadtreeRenderer draws with zero bound vertex
// buffers, and the vertex shader decodes (col,row) directly from
// gl_VertexIndex (itself sourced from THIS index buffer's values). Height/
// normal are sampled procedurally via VTF from TerrainWorldHeightmap.
//
// Skirts and stitched/zippered border variants (the old adaptive-LOD
// system's way of hiding density mismatches between differently-sized
// neighbors) are gone: every tile is emitted at the SAME fixed depth
// (TerrainQuadtree), so neighbors always share identical vertex density at
// their border -- there is no mismatch to hide.
//
// Index-value range (all indices, real Vulkan gl_VertexIndex values):
//   [0, 289) -- regular 17x17 grid, idx = row*17 + col
namespace md {

struct TerrainQuadtreeMesh {
    std::vector<uint32_t> filled_indices; // full 16x16 grid
    static constexpr int kPatchQuads = 16;
    static constexpr int kGridSize   = kPatchQuads + 1; // 17
    static constexpr int kRegularVertexCount = kGridSize * kGridSize; // 289
};

// Builds the ONE shared index buffer (called once, at renderer Init()).
TerrainQuadtreeMesh BuildTerrainQuadtreeMesh();

} // namespace md
