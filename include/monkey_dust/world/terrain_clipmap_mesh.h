#pragma once
#include <cstdint>
#include <vector>

// GEOCLIPMAP Phase 3 (serene-pondering-teapot.md): the shared mesh
// topology every clip level draws with. Unlike the old Geomipmapping
// system (TerrainPatchRenderer, one static mesh PER tier, 8 total), a
// clipmap needs only TWO static meshes for ALL levels -- every level has
// the identical (N+1)x(N+1) vertex grid and local UV range [0,1]^2, just
// drawn at a different per-level world-space origin/scale (see the
// per-level UBO in terrain_clipmap_renderer.h). Only the level-0 (finest,
// no finer level inside it) draw uses the "filled" index buffer; every
// other level uses "ring" -- the same quad grid with (nominally) the
// central N/2 x N/2 quads excluded, shrunk by 1 quad margin on every
// edge in practice (see BuildClipmapMesh's body doc comment) because
// TerrainClipmapCache::Recenter snaps each level's origin independently
// from raw camera position, not guaranteed to align exactly with the
// next-finer level's actual footprint -- the margin trades a thin,
// coplanar overlap strip for guaranteeing the hole never opens a
// world-space gap (found 2026-08-16, "square rings around camera" at
// aerial altitude).

namespace md {

struct ClipmapMeshVertex {
    float u, v; // local UV in [0,1]^2, same convention as terrain_patch_gbuffer.vert's aUVSkirt.xy
};

struct ClipmapMesh {
    std::vector<ClipmapMeshVertex> vertices;  // (N+1)*(N+1), shared by both index buffers
    std::vector<uint32_t> filled_indices;     // level 0 only: every quad, N*N*6 indices
    std::vector<uint32_t> ring_indices;       // levels 1..L-1: every quad EXCEPT the central (N/2)*(N/2) hole
};

// N must be even (so the central hole boundary [N/4, 3N/4) lands on exact
// quad indices) and a multiple of 4 in practice (kPatchN=128 precedent).
ClipmapMesh BuildClipmapMesh(int N);

} // namespace md
