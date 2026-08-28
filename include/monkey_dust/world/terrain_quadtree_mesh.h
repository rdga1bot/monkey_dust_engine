#pragma once
#include <cstdint>
#include <vector>

// Final terrain architecture (2026-08-18, plan at serene-pondering-teapot.md,
// "Ogre-quadtree (geomorph+skirts)"): a SHARED, tiny index buffer describing
// ONE node's topology -- patchSize=16 (16x16 quads, 17x17 grid positions),
// matching real Kenshi's own "standard" Ogre::Terrain patch size exactly
// (re_docs/kenshi/terrain.md Subsystem 5). Every quadtree node at every
// depth, everywhere in the world, reuses this SAME index buffer (only a
// per-node origin/texelSize/morph/skirtDepth UBO push differs) -- there is
// deliberately NO per-node vertex buffer: TerrainQuadtreeRenderer draws
// with zero bound vertex buffers (matches the already-proven Phase 1 spike
// technique, terrain_patch_novbo_spike.vert), and the vertex shader decodes
// (col,row) directly from gl_VertexIndex (itself sourced from THIS index
// buffer's values -- Vulkan's gl_VertexIndex for an indexed draw comes from
// the index buffer regardless of whether any vertex ATTRIBUTE buffer is
// bound). Height/normal are sampled procedurally via VTF from
// TerrainWorldHeightmap + the Phase-3 world-wide baked normal texture, at
// TWO texel steps (this node's own, and 2x for the coarser/parent LOD) --
// giving geomorph without any load-time per-node vertex baking, which is
// what the real Ogre::Terrain source (this session's DeepSeek research
// citing OgreTerrainQuadTreeNode.cpp) has to do BECAUSE it lacks our GPU's
// VTF access. This keeps the crucial post-transform-cache benefit an index
// buffer provides (PTC keys off INDEX VALUES, not attribute-buffer
// presence -- confirmed via this session's own measured regression:
// Phase 2's non-indexed spike had 6 UNSHARED vertex indices per quad, zero
// PTC reuse, 1.48-2.59x slower; a real index buffer with shared corner
// indices restores PTC reuse even with layout.count=0).
//
// Index-value ranges (all indices, real Vulkan gl_VertexIndex values):
//   [0, 289)            -- regular 17x17 grid, idx = row*17 + col
//   [289, 289+4*16)      -- 4 border skirt strips, 16 duplicate-vertex
//                           positions each (one per edge quad corner along
//                           that edge, EXCLUDING the shared corner already
//                           covered by the adjacent edge's strip -- see
//                           .cpp for the exact decode formula). The vertex
//                           shader maps a skirt index back to its
//                           corresponding border (col,row) and subtracts
//                           skirtDepth from height, exactly like a normal
//                           border vertex except lowered.
namespace md {

struct TerrainQuadtreeMesh {
    std::vector<uint32_t> filled_indices; // full 16x16 grid, no skirts
    std::vector<uint32_t> skirt_indices;  // 4 border strips (separate draw, or appended)
    static constexpr int kPatchQuads = 16;
    static constexpr int kGridSize   = kPatchQuads + 1; // 17
    static constexpr int kRegularVertexCount = kGridSize * kGridSize; // 289
};

// Builds the ONE shared index buffer (called once, at renderer Init()).
TerrainQuadtreeMesh BuildTerrainQuadtreeMesh();

// docs/OPENMW_TERRAIN_BORROWED_TECHNIQUES.md Phase 2: builds a stitched,
// skirt-free index buffer for ONE of 16 border configurations. edgeMask
// bit 0=north(row=16) 1=south(row=0) 2=east(col=16) 3=west(col=0); a set
// bit means that edge borders a neighbor exactly ONE quadtree depth level
// coarser and needs its outer ring "zippered": every other border vertex
// is dropped from the outer silhouette (its true height is simply not
// used there) so the remaining 9-of-17 border points exactly match the
// coarser neighbor's own 9 border points -- eliminating the T-junction
// crack structurally, with zero vertical skirt geometry. Corner quads
// are always plain (never zippered) -- a quadtree corner point is always
// a shared anchor between a node and every possible neighbor depth, so
// it never has a fine/coarse density mismatch to resolve (unlike edge
// midpoints).
//
// ONLY a 1-level gap is representable this way; a >=2-level gap (rare --
// see QuadtreeBalanceNeighbors's own "no gap, or gap<2" comment) needs
// the old skirt path as a fallback (not handled by this function).
//
// Requires TerrainQuadtree::LodMode()==1 (OpenMW-style discrete LOD, no
// morph): continuous CDLOD morphing (mode 0, default) interpolates a
// border vertex's world position continuously every frame between its
// own-LOD and parent-LOD height sample, which a STATIC index-buffer weld
// cannot track -- see terrain_quadtree.h's SetLodMode doc comment. This
// is why mode 1 forces morph=0 unconditionally.
std::vector<uint32_t> BuildTerrainQuadtreeStitchedIndices(uint8_t edgeMask);

} // namespace md
