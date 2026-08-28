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

namespace {
// Emits one plain quad (c,r)-(c+1,r)-(c,r+1)-(c+1,r+1), same winding as
// BuildTerrainQuadtreeMesh's own interior-fill loop above.
inline void EmitPlainQuad(std::vector<uint32_t>& out, int c, int r) {
    uint32_t i0 = RegularIdx(c, r);
    uint32_t i1 = RegularIdx(c + 1, r);
    uint32_t i2 = RegularIdx(c, r + 1);
    uint32_t i3 = RegularIdx(c + 1, r + 1);
    out.push_back(i0); out.push_back(i2); out.push_back(i1);
    out.push_back(i1); out.push_back(i2); out.push_back(i3);
}
} // namespace

std::vector<uint32_t> BuildTerrainQuadtreeStitchedIndices(uint8_t edgeMask) {
    constexpr int N = TerrainQuadtreeMesh::kPatchQuads; // 16
    std::vector<uint32_t> out;
    out.reserve((size_t)N * N * 6);

    const bool north = (edgeMask & 0x1) != 0;
    const bool south = (edgeMask & 0x2) != 0;
    const bool east  = (edgeMask & 0x4) != 0;
    const bool west  = (edgeMask & 0x8) != 0;

    // Core interior: quad-rows/cols 1..N-2, never touched by any edge
    // treatment (identical for all 16 configurations).
    for (int row = 1; row <= N - 2; ++row) {
        for (int col = 1; col <= N - 2; ++col) {
            EmitPlainQuad(out, col, row);
        }
    }

    // 4 corner quads -- always plain, see this function's own doc comment
    // (terrain_quadtree_mesh.h) for why corners never need zippering.
    EmitPlainQuad(out, 0,     0);     // SW
    EmitPlainQuad(out, N - 1, 0);     // SE
    EmitPlainQuad(out, 0,     N - 1); // NW
    EmitPlainQuad(out, N - 1, N - 1); // NE

    // North strip (border row=N, interior row=N-1), quad-cols 1..N-2.
    // Winding hand-derived + signed-area-verified per edge (see Phase 2
    // plan doc / commit message) -- north and west need swapped triangle
    // order relative to south/east, matching the SAME asymmetry the
    // existing skirt-strip loop's own winding already has by construction.
    if (!north) {
        for (int col = 1; col <= N - 2; ++col) EmitPlainQuad(out, col, N - 1);
    } else {
        for (int c0 = 1; c0 <= N - 3; c0 += 2) {
            uint32_t F0 = RegularIdx(c0,     N - 1);
            uint32_t F1 = RegularIdx(c0 + 1, N - 1);
            uint32_t F2 = RegularIdx(c0 + 2, N - 1);
            uint32_t Q0 = RegularIdx(c0,     N);
            uint32_t Q1 = RegularIdx(c0 + 2, N);
            out.push_back(F0); out.push_back(Q0); out.push_back(F1);
            out.push_back(F1); out.push_back(Q0); out.push_back(Q1);
            out.push_back(F1); out.push_back(Q1); out.push_back(F2);
        }
    }

    // South strip (border row=0, interior row=1), quad-cols 1..N-2.
    if (!south) {
        for (int col = 1; col <= N - 2; ++col) EmitPlainQuad(out, col, 0);
    } else {
        for (int c0 = 1; c0 <= N - 3; c0 += 2) {
            uint32_t F0 = RegularIdx(c0,     1);
            uint32_t F1 = RegularIdx(c0 + 1, 1);
            uint32_t F2 = RegularIdx(c0 + 2, 1);
            uint32_t Q0 = RegularIdx(c0,     0);
            uint32_t Q1 = RegularIdx(c0 + 2, 0);
            out.push_back(F0); out.push_back(F1); out.push_back(Q0);
            out.push_back(F1); out.push_back(Q1); out.push_back(Q0);
            out.push_back(F1); out.push_back(F2); out.push_back(Q1);
        }
    }

    // East strip (border col=N, interior col=N-1), quad-rows 1..N-2.
    if (!east) {
        for (int row = 1; row <= N - 2; ++row) EmitPlainQuad(out, N - 1, row);
    } else {
        for (int r0 = 1; r0 <= N - 3; r0 += 2) {
            uint32_t F0 = RegularIdx(N - 1, r0);
            uint32_t F1 = RegularIdx(N - 1, r0 + 1);
            uint32_t F2 = RegularIdx(N - 1, r0 + 2);
            uint32_t Q0 = RegularIdx(N,     r0);
            uint32_t Q1 = RegularIdx(N,     r0 + 2);
            out.push_back(F0); out.push_back(F1); out.push_back(Q0);
            out.push_back(F1); out.push_back(Q1); out.push_back(Q0);
            out.push_back(F1); out.push_back(F2); out.push_back(Q1);
        }
    }

    // West strip (border col=0, interior col=1), quad-rows 1..N-2.
    if (!west) {
        for (int row = 1; row <= N - 2; ++row) EmitPlainQuad(out, 0, row);
    } else {
        for (int r0 = 1; r0 <= N - 3; r0 += 2) {
            uint32_t F0 = RegularIdx(1, r0);
            uint32_t F1 = RegularIdx(1, r0 + 1);
            uint32_t F2 = RegularIdx(1, r0 + 2);
            uint32_t Q0 = RegularIdx(0, r0);
            uint32_t Q1 = RegularIdx(0, r0 + 2);
            out.push_back(F0); out.push_back(Q0); out.push_back(F1);
            out.push_back(F1); out.push_back(Q0); out.push_back(Q1);
            out.push_back(F1); out.push_back(Q1); out.push_back(F2);
        }
    }

    return out;
}

} // namespace md
