#include <monkey_dust/world/terrain_clipmap_mesh.h>

namespace md {

ClipmapMesh BuildClipmapMesh(int N) {
    ClipmapMesh out;
    out.vertices.reserve((size_t)(N + 1) * (size_t)(N + 1));
    for (int row = 0; row <= N; ++row) {
        for (int col = 0; col <= N; ++col) {
            out.vertices.push_back({(float)col / (float)N, (float)row / (float)N});
        }
    }

    auto vidx = [N](int col, int row) -> uint32_t {
        return (uint32_t)(row * (N + 1) + col);
    };

    // Central hole: nominally [N/4, 3N/4) x [N/4, 3N/4) in quad-index
    // space -- half the edge length, centered, matching the next-finer
    // level's world-space footprint (that level's own full N-quad span
    // covers a world area exactly half this level's, per the
    // texel-doubling invariant).
    //
    // hole_lo is nudged in by 1 quad deliberately: TerrainClipmapCache::
    // Recenter snaps each level's origin independently from raw camera
    // position (floor((cam-half_world)/ts)), which does NOT generally
    // land the nominal hole exactly on the next-finer level's actual
    // footprint. Working the algebra (both levels' floor() share the
    // same non-integer -64.5 texel centering offset, at different
    // scales -- NOT the textbook floor(x)-2*floor(x/2) identity some
    // early analysis assumed) shows the finer level's footprint origin,
    // relative to this level's nominal hole origin, is ALWAYS shifted by
    // a fixed non-negative amount in {0,1,2} next-finer-level texels,
    // never negative and never larger -- i.e. the mismatch is one-sided,
    // not a symmetric ±1 wobble. hole_hi therefore needs NO adjustment
    // (the nominal value is already the exact worst-case-safe bound: it
    // matches when the shift is 0, the only case that constrains it);
    // only hole_lo needs to move inward by ceil(2/2)=1 quad to stay
    // inside the footprint at the shift's maximum of 2 (found
    // 2026-08-16, "square rings around camera" at aerial altitude --
    // verified both by direct derivation and a dense numeric sweep over
    // camera positions x all 7 level pairs, 0 containment violations).
    // Shrinking hole_lo (not hole_hi) makes the ring hole a genuine
    // subset of the footprint in every case, trading a thin coplanar
    // overlap strip (safe) for guaranteeing it never opens a gap --
    // this plan's own stated "generous overlap over fiddly
    // parity-stitching" compromise, just asymmetric because the
    // underlying mismatch itself is asymmetric.
    const int hole_lo = N / 4 + 1;
    const int hole_hi = (3 * N) / 4;

    out.filled_indices.reserve((size_t)N * (size_t)N * 6);
    const int hole_span = hole_hi - hole_lo;
    const size_t hole_quads = (size_t)hole_span * (size_t)hole_span;
    out.ring_indices.reserve((size_t)N * (size_t)N * 6 - hole_quads * 6);

    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            uint32_t i0 = vidx(col, row);
            uint32_t i1 = vidx(col + 1, row);
            uint32_t i2 = vidx(col, row + 1);
            uint32_t i3 = vidx(col + 1, row + 1);

            out.filled_indices.push_back(i0);
            out.filled_indices.push_back(i2);
            out.filled_indices.push_back(i1);
            out.filled_indices.push_back(i1);
            out.filled_indices.push_back(i2);
            out.filled_indices.push_back(i3);

            bool in_hole = col >= hole_lo && col < hole_hi && row >= hole_lo && row < hole_hi;
            if (!in_hole) {
                out.ring_indices.push_back(i0);
                out.ring_indices.push_back(i2);
                out.ring_indices.push_back(i1);
                out.ring_indices.push_back(i1);
                out.ring_indices.push_back(i2);
                out.ring_indices.push_back(i3);
            }
        }
    }

    return out;
}

} // namespace md
