#include <monkey_dust/world/terrain_quadtree_mesh.h>

namespace md {

namespace {
inline uint32_t RegularIdx(int col, int row) {
    return (uint32_t)(row * TerrainQuadtreeMesh::kGridSize + col);
}
} // namespace

TerrainQuadtreeMesh BuildTerrainQuadtreeMesh() {
    TerrainQuadtreeMesh out;
    constexpr int N = TerrainQuadtreeMesh::kPatchQuads; // 16

    // Regular 16x16 filled grid -- same winding convention this codebase's
    // other shared meshes use (i0,i2,i1 / i1,i2,i3): i0=(c,r) i1=(c+1,r)
    // i2=(c,r+1) i3=(c+1,r+1).
    out.filled_indices.reserve((size_t)N * N * 6);
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            uint32_t i0 = RegularIdx(col, row);
            uint32_t i1 = RegularIdx(col + 1, row);
            uint32_t i2 = RegularIdx(col, row + 1);
            uint32_t i3 = RegularIdx(col + 1, row + 1);
            out.filled_indices.push_back(i0);
            out.filled_indices.push_back(i2);
            out.filled_indices.push_back(i1);
            out.filled_indices.push_back(i1);
            out.filled_indices.push_back(i2);
            out.filled_indices.push_back(i3);
        }
    }

    return out;
}

} // namespace md
