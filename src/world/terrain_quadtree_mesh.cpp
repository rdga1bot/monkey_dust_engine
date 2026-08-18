#include <monkey_dust/world/terrain_quadtree_mesh.h>

namespace md {

namespace {
inline uint32_t RegularIdx(int col, int row) {
    return (uint32_t)(row * TerrainQuadtreeMesh::kGridSize + col);
}
// Matches the shader's decode: border*17 + along, offset by kRegularVertexCount.
// border: 0=north(row=16) 1=south(row=0) 2=east(col=16) 3=west(col=0)
inline uint32_t SkirtIdx(int border, int along) {
    return (uint32_t)(TerrainQuadtreeMesh::kRegularVertexCount + border * TerrainQuadtreeMesh::kGridSize + along);
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

    // 4 border skirt strips. Each border has N=16 quads connecting the
    // border's 17 regular (top) vertices to 17 skirt (bottom, lowered by
    // skirtDepth in the vertex shader) duplicate vertices. Corners are
    // computed independently by each of the two borders that touch them
    // (harmless -- same world position either way, just decoded twice).
    out.skirt_indices.reserve((size_t)4 * N * 6);
    for (int border = 0; border < 4; ++border) {
        for (int along = 0; along < N; ++along) {
            int col0, row0, col1, row1;
            switch (border) {
                case 0: col0 = along; row0 = N; col1 = along + 1; row1 = N; break; // north
                case 1: col0 = along; row0 = 0; col1 = along + 1; row1 = 0; break; // south
                case 2: col0 = N; row0 = along; col1 = N; row1 = along + 1; break; // east
                default: col0 = 0; row0 = along; col1 = 0; row1 = along + 1; break; // west
            }
            uint32_t top0 = RegularIdx(col0, row0);
            uint32_t top1 = RegularIdx(col1, row1);
            uint32_t bot0 = SkirtIdx(border, along);
            uint32_t bot1 = SkirtIdx(border, along + 1);
            out.skirt_indices.push_back(top0);
            out.skirt_indices.push_back(bot0);
            out.skirt_indices.push_back(top1);
            out.skirt_indices.push_back(top1);
            out.skirt_indices.push_back(bot0);
            out.skirt_indices.push_back(bot1);
        }
    }

    return out;
}

} // namespace md
