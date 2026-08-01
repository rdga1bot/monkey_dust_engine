#pragma once

// TERRAIN_CA_REBUILD_PROMPT.md Phase 2 — pure CPU math for baking a
// TerrainPatchRenderer tier-mesh's per-vertex heights from the real
// world heightmap (TerrainAtlas_SampleWorld). No GPU dependency,
// unit-testable in isolation from any renderer/device.
//
// Reuses the EXACT SAME (u,v) grid + skirt-vertex topology as
// TerrainPatchRenderer::BuildTierMesh (surface verts row-major
// (N+1)x(N+1), then 4 edge skirt strips of (N+1) verts each appended
// after) -- a caller can reuse that class's existing per-tier INDEX
// buffers unchanged (topology is identical between the VTF and baked
// paths; only the per-vertex DATA differs -- uv-only + runtime texture
// sample vs uv + precomputed height).
//
// height_fine: direct TerrainAtlas_SampleWorld sample at this vertex's
// exact world position -- tier-independent (no mip blending needed:
// the mesh's own vertex density IS the LOD reduction, unlike the VTF
// path's textureLod mip selection).
//
// height_coarse: bilinear interpolation of the NEXT-COARSER tier's 4
// surrounding grid-corner heights at this exact vertex position --
// what the coarser mesh's own piecewise-linear surface would show
// here. Used by the vertex shader's existing CDLOD geomorph blend
// (mix(height_fine, height_coarse, morphT)), same role height_coarse
// would play if resampled from a texture at the snapped UV, just
// precomputed instead of resampled at runtime.
//
// Cross-tier T-junction note: baking from the real heightmap does NOT
// eliminate the cross-tier crack (verified by construction, not
// guessed) -- adjacent DIFFERENT-tier patches still show a residual
// gap at their shared edge, because a coarser mesh's piecewise-linear
// interpolation between its own sparser vertices is a geometrically
// different curve than a finer mesh's interpolation between its denser
// ones, even when every vertex sources the identical raw heightmap
// function. Skirt vertices (the `skirt`=1 rows appended after the
// surface grid, same treatment as terrain_patch.vert's SKIRT_DEPTH_M)
// still apply here for the same reason they did in the VTF path.
// SAME-tier adjacent patches, by contrast, DO share bit-identical
// height_fine at their boundary (both sample TerrainAtlas_SampleWorld
// at the exact same world position) -- verified in
// tests/test_terrain_bake.cpp.
struct TerrainBakedVertex {
    float u, v;
    float height_fine;
    float height_coarse;
    float skirt;
};

// Same vertex/index COUNT formulas TerrainPatchRenderer::BuildTierMesh
// uses for a tier with `quads_per_edge` (N) quads/edge -- kept
// explicitly in sync (both hardcode the same math) so callers can size
// buffers without depending on the renderer's private statics.
int TerrainBake_VertexCount(int quads_per_edge);
int TerrainBake_IndexCount(int quads_per_edge);

// Plain function pointer, not std::function -- no heap/exception overhead
// in a routine called thousands of times per bake. Production call sites
// pass TerrainAtlas_SampleWorld (engine/include/monkey_dust/world/
// terrain_gen.h); tests inject a synthetic height field instead of
// depending on a real (136MB) loaded atlas file.
using TerrainHeightSampleFn = float (*)(float world_x, float world_z);

// Fills out_verts (must hold at least TerrainBake_VertexCount(quads_per_edge)
// entries) for a patch at world-space (origin_x, origin_z), size
// patch_size, tier resolution quads_per_edge. has_coarser=false for the
// coarsest tier (no coarser neighbor to morph toward -- height_coarse
// is set equal to height_fine).
void TerrainBake_ComputeVertices(float origin_x, float origin_z,
                                  float patch_size, int quads_per_edge,
                                  bool has_coarser,
                                  TerrainHeightSampleFn sample_height,
                                  TerrainBakedVertex* out_verts);
