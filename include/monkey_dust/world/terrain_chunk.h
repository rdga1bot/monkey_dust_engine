#pragma once
#include <cstdint>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/nav/navmesh.h>
#include <monkey_dust/world/chunk_def.h>
#include <monkey_dust/world/terrain_pass_grid.h>

// Geomorphing: blends vertex Y toward coarser-LOD target beyond 420m (ramp 420–600m).
// morph_y filled by TerrainGen_Build; consumed by terrain_pom.vert.
//
// NOTE: this flag is a SEPARATE preprocessor from shaders/terrain_pom.vert's
// own `TERRAIN_GEOMORPH_ENABLED` (C++ vs GLSL, no shared preprocessor —
// confirmed by independent audit, docs/ENGINE_AUDIT.md/TERRAIN_FIX_PROMPT.md).
// This one only gates whether morph_y is COMPUTED on the CPU (currently
// always on); the shader's flag independently gates whether the GPU
// actually USES it (currently off). Flipping this one alone does nothing
// visually — see shaders/terrain_pom.vert:9's matching comment.
#define TERRAIN_MORPH_DATA_ENABLED 1

// Terrain grid: 128×128 quads = 129×129 vertices per chunk (CHUNK_SIZE=460.8m
// -> 3.6m/quad). Matches Kenshi's own real in-engine resolution (RE-confirmed:
// raw 258x258 tile fetch downsampled internally to 129x129/zone,
// re_docs/kenshi/terrain.md) — was 64/65 (coarser than Kenshi itself).
static constexpr int   TERRAIN_GRID  = 128;
static constexpr int   TERRAIN_VERTS = (TERRAIN_GRID + 1) * (TERRAIN_GRID + 1); // 16641
static constexpr int   TERRAIN_TRIS  = TERRAIN_GRID * TERRAIN_GRID * 2;         // 32768
static constexpr int   TERRAIN_IDX   = TERRAIN_TRIS * 3;                         // 98304
static constexpr float TERRAIN_STEP  = CHUNK_SIZE / TERRAIN_GRID;               // 3.6m

// LOD levels (same VBO, coarser IBO — steps of 2/4/8 vertices)
static constexpr int   TERRAIN_LOD_LEVELS    = 3;
static constexpr int   TERRAIN_LOD_STEPS[3]  = { 2, 4, 8 };
// 2026-07-19: sizes account for BuildLodIboStitched's full-resolution
// border (task #182 T-junction fix — see that function's doc comment).
// triangles = 2*(G-2)^2 + 4*(G-2)*(step+1) + 8*step, G=TERRAIN_GRID/step.
static constexpr int   TERRAIN_LOD_IDX[3]    = {
    25344,  // step=2, G=64:  8448 tris (was 24576/8192 tris pre-stitch)
     7296,  // step=4, G=32:  2432 tris (was  6144/2048 tris pre-stitch)
     2880   // step=8, G=16:   960 tris (was  1536/ 512 tris pre-stitch)
};
// Camera-to-chunk-centre distance thresholds to switch LOD levels
static constexpr float TERRAIN_LOD_DIST[3]   = { 600.f, 1200.f, 2000.f };

// Vertex layout (stride = 52 bytes):
//   location 0: vec3 pos    (12)
//   location 1: vec3 normal (12)
//   location 2: vec2 uv     (8)   — world-space UV for texture tiling
//   location 3: vec4 ground (16)  — unused, see ground_id's doc comment
struct TerrainVertex {
    float x, y, z;           // world position        offset  0
    float nx, ny, nz;         // normal                offset 12
    float u, v;               // UV (world-space)      offset 24
    // Dominant-weight ground selection (base/slope/cliff/grass/dirt/road
    // argmax, re_docs/kenshi/terrain.md:114-136) runs PER-PIXEL in
    // terrain_forward.slang's fsMain now, not baked here. A same-day
    // 2026-07-19 first attempt baked ground_id/ground_id2 into these
    // fields (flat-interpolated, since array-layer indices can't be
    // lerped) — real bug: adjacent vertices on a slope often disagree on
    // the dominant layer, and flat interpolation paints each whole
    // triangle from one vertex's decision, producing dense triangle-shaped
    // seams (confirmed via screenshots). fsMain now recomputes the same
    // argmax per-pixel from the already-smoothly-interpolated normal and a
    // live tex_overlay_mask sample instead. These 4 floats are unused
    // leftover slots — same status `splat[4]` had before that whole
    // rewrite (was [grass,rock,dirt,snow], computed on CPU but never
    // sampled by any shader). Kept (not removed) purely to avoid touching
    // vertex stride/layout across every caller for an already-dead field.
    float ground_id;          // unused
    float ground_id2;         // unused
    float blend_alpha;        // unused
    float _reserved;          // unused
    float morph_y;            // geomorph target Y for L0→L1; guarded by TERRAIN_MORPH_DATA_ENABLED
};
static_assert(sizeof(TerrainVertex) == 52, "TerrainVertex size mismatch");

// Builds an LOD index buffer whose CHUNK BOUNDARY is always drawn at full
// resolution (step=1), regardless of how decimated the interior is (step
// S ∈ {2,4,8}, TERRAIN_LOD_STEPS). This is a geomorphic seam-stitch: since
// every chunk ALWAYS draws its own border at full resolution, any two
// adjacent chunks' shared edge matches exactly for ANY combination of LOD
// tiers — no runtime cross-chunk coordination needed, each chunk is
// self-sufficient. Fixes a confirmed T-junction gap (measured up to
// 19.4m — md.scan_terrain_seam_tjunctions()) where the old scheme
// decimated the border too, leaving a straight-line approximation that
// could diverge sharply from the real (fine) terrain on steep Kenshi
// cliffs (see project_pillar_bug_confirmed.md memory / task #182).
//
// Reuses the SAME shared VBO every LOD tier already uses — normals are
// computed once from the full-resolution mesh (terrain_gen.cpp's normal-
// accumulation pass covers every vertex regardless of which LOD IBO ends
// up drawn), so this only changes which triangles get drawn. No new
// vertices, no new normal computation.
//
// Three parts, each independently derived and winding-verified (2D signed
// area in (col,row) space, matching the plain-decimation interior's own
// CCW convention — the border/corner winding is NOT symmetric across all
// 4 sides, verified individually, do not "simplify" by assuming symmetry):
//   1. Core interior: same 2-triangle-per-quad pattern as before, shifted
//      inward by one decimated cell (that outer ring is replaced by parts
//      2+3 below).
//   2. Border strips (4 edges): a "zipper" between the full-res chunk
//      edge (step+1 fine points per decimated span) and the first
//      interior line (2 coarse points per span) — step+1 triangles/span.
//   3. Corner patches (4 corners): a pure fan from the single interior
//      corner anchor to the L-shaped full-res chain of the two adjacent
//      edges — 2*step triangles/corner.
// out must have room for TERRAIN_LOD_IDX[lod-1] uint16_t (lod=1,2,3).
// Returns the actual index count written (3 * triangle count).
inline int BuildLodIboStitched(uint16_t* out, int step) {
    const int G = TERRAIN_GRID / step;
    const int ROWLEN = TERRAIN_GRID + 1;
    auto idx = [&](int col, int row) -> uint16_t {
        return (uint16_t)(row * ROWLEN + col);
    };
    int ii = 0;
    auto emit = [&](uint16_t a, uint16_t b, uint16_t c) {
        out[ii++] = a; out[ii++] = b; out[ii++] = c;
    };

    // ── 1. Core interior ───────────────────────────────────────────────
    for (int kr = 1; kr <= G - 2; ++kr) {
        for (int kc = 1; kc <= G - 2; ++kc) {
            uint16_t bl = idx(kc*step,      kr*step);
            uint16_t br = idx(kc*step+step, kr*step);
            uint16_t tl = idx(kc*step,      kr*step+step);
            uint16_t tr = idx(kc*step+step, kr*step+step);
            emit(bl, br, tl);
            emit(br, tr, tl);
        }
    }

    const int mid = step / 2;  // step is always an even power of 2 here

    // ── 2. Border strips ───────────────────────────────────────────────
    // Bottom (row=0): fine F[i]=idx(i,0); coarse Q[k]=idx(k*step,step).
    for (int k = 1; k <= G - 2; ++k) {
        uint16_t Q0 = idx(k*step,     step);
        uint16_t Q1 = idx((k+1)*step, step);
        for (int i = 0; i < mid; ++i)
            emit(idx(k*step+i, 0), idx(k*step+i+1, 0), Q0);
        emit(idx(k*step+mid, 0), Q1, Q0);
        for (int i = mid; i < step; ++i)
            emit(idx(k*step+i, 0), idx(k*step+i+1, 0), Q1);
    }
    // Right (col=GRID): fine F[j]=idx(GRID,j); coarse Q[k]=idx(GRID-step,k*step).
    // Same (unreversed) winding as bottom.
    for (int k = 1; k <= G - 2; ++k) {
        uint16_t Q0 = idx(TERRAIN_GRID-step, k*step);
        uint16_t Q1 = idx(TERRAIN_GRID-step, (k+1)*step);
        for (int j = 0; j < mid; ++j)
            emit(idx(TERRAIN_GRID, k*step+j), idx(TERRAIN_GRID, k*step+j+1), Q0);
        emit(idx(TERRAIN_GRID, k*step+mid), Q1, Q0);
        for (int j = mid; j < step; ++j)
            emit(idx(TERRAIN_GRID, k*step+j), idx(TERRAIN_GRID, k*step+j+1), Q1);
    }
    // Top (row=GRID): fine F[i]=idx(i,GRID); coarse Q[k]=idx(k*step,GRID-step).
    // Reversed winding relative to bottom.
    for (int k = 1; k <= G - 2; ++k) {
        uint16_t Q0 = idx(k*step,     TERRAIN_GRID-step);
        uint16_t Q1 = idx((k+1)*step, TERRAIN_GRID-step);
        for (int i = 0; i < mid; ++i)
            emit(idx(k*step+i+1, TERRAIN_GRID), idx(k*step+i, TERRAIN_GRID), Q0);
        emit(idx(k*step+mid, TERRAIN_GRID), Q0, Q1);
        for (int i = mid; i < step; ++i)
            emit(idx(k*step+i+1, TERRAIN_GRID), idx(k*step+i, TERRAIN_GRID), Q1);
    }
    // Left (col=0): fine F[j]=idx(0,j); coarse Q[k]=idx(step,k*step).
    // Reversed winding relative to bottom.
    for (int k = 1; k <= G - 2; ++k) {
        uint16_t Q0 = idx(step, k*step);
        uint16_t Q1 = idx(step, (k+1)*step);
        for (int j = 0; j < mid; ++j)
            emit(idx(0, k*step+j+1), idx(0, k*step+j), Q0);
        emit(idx(0, k*step+mid), Q0, Q1);
        for (int j = mid; j < step; ++j)
            emit(idx(0, k*step+j+1), idx(0, k*step+j), Q1);
    }

    // ── 3. Corner patches ──────────────────────────────────────────────
    // Winding verified independently per corner — checkerboard pattern
    // (BL/TR reversed, BR/TL unreversed), not a typo.
    uint16_t chain[2*8+1];  // max size at step=8

    // Bottom-left: anchor idx(step,step), chain along row=0 then col=0.
    {
        uint16_t A = idx(step, step);
        int n = 0;
        for (int m = 0; m <= step; ++m) chain[n++] = idx(step-m, 0);
        for (int m = 1; m <= step; ++m) chain[n++] = idx(0, m);
        for (int i = 0; i < n-1; ++i) emit(chain[i+1], chain[i], A);
    }
    // Bottom-right: anchor idx(GRID-step,step), chain along row=0 then col=GRID.
    {
        uint16_t A = idx(TERRAIN_GRID-step, step);
        int n = 0;
        for (int m = 0; m <= step; ++m) chain[n++] = idx(TERRAIN_GRID-step+m, 0);
        for (int m = 1; m <= step; ++m) chain[n++] = idx(TERRAIN_GRID, m);
        for (int i = 0; i < n-1; ++i) emit(chain[i], chain[i+1], A);
    }
    // Top-left: anchor idx(step,GRID-step), chain along row=GRID then col=0.
    {
        uint16_t A = idx(step, TERRAIN_GRID-step);
        int n = 0;
        for (int m = 0; m <= step; ++m) chain[n++] = idx(step-m, TERRAIN_GRID);
        for (int m = 1; m <= step; ++m) chain[n++] = idx(0, TERRAIN_GRID-m);
        for (int i = 0; i < n-1; ++i) emit(chain[i], chain[i+1], A);
    }
    // Top-right: anchor idx(GRID-step,GRID-step), chain along row=GRID then col=GRID.
    {
        uint16_t A = idx(TERRAIN_GRID-step, TERRAIN_GRID-step);
        int n = 0;
        for (int m = 0; m <= step; ++m) chain[n++] = idx(TERRAIN_GRID-step+m, TERRAIN_GRID);
        for (int m = 1; m <= step; ++m) chain[n++] = idx(TERRAIN_GRID, TERRAIN_GRID-m);
        for (int i = 0; i < n-1; ++i) emit(chain[i+1], chain[i], A);
    }

    return ii;
}

// Raw heightmap data — stored separately for CPU-side queries (NPC grounding, etc.)
struct TerrainHeightmap {
    float h[TERRAIN_VERTS];   // [row * (TERRAIN_GRID+1) + col]
};

// ── Per-chunk procedural props ────────────────────────────────────────────────
// Props are generated during PropGen_Build (game-side) and stored per-chunk.
// They stream in/out automatically with terrain chunk loading.
enum ChunkPropType : uint8_t {
    kPropRock      = 0,  // rock_01       (~2 m)
    kPropFormation = 1,  // rock_formation (~2 m)
    kPropHatRock   = 2,  // hat_rock       (~5 m, dramatic, rare)
    kPropYucca     = 3,  // yucca          (~1.7 m)
    kPropCanyonRock= 4,  // canyon_rock    (~1.3 m, small stony plant)
    kPropDeadTree  = 5,  // dead_tree      (~1.8 m)
    kPropBones     = 6,  // bones          (ribcage, ~1.5 m, wasteland dressing)
    kPropRuinJunk  = 7,  // ruin_junk      (concrete rubble chunk, ~1 m)
    kPropSpike     = 8,  // spike_bloom    (hazard plant/spike cluster, ~2 m, rare — heavy mesh)
    kPropTypeCount = 9
};

struct ChunkPropInstance {
    float   x, y, z;    // world-space position (y already accounts for embed)
    float   nx, ny, nz; // terrain surface normal (sampled by PropGen_Build, G-2)
    uint8_t type;        // ChunkPropType
    uint8_t _pad[3];
};
static_assert(sizeof(ChunkPropInstance) == 28, "ChunkPropInstance size");
static constexpr int CHUNK_MAX_PROPS = 64;

// Skirt geometry constants: 4 edges × (TERRAIN_GRID+1) × 2 verts (top+bottom)
static constexpr int TERRAIN_SKIRT_VERTS = 4 * (TERRAIN_GRID + 1) * 2;  // 520
static constexpr int TERRAIN_SKIRT_IDX   = 4 *  TERRAIN_GRID * 6;       // 1536

struct TerrainChunk {
    ChunkCoord      coord;
    float           center_x = 0.f;    // world-space centre (set by TerrainGen_Build)
    float           center_z = 0.f;
    GpuStaticBuffer vbo;               // TerrainVertex * TERRAIN_VERTS
    GpuStaticBuffer ibo;               // uint16_t * TERRAIN_IDX  (L0: 128×128)
    GpuStaticBuffer ibo_lod[3];        // L1: 64×64, L2: 32×32, L3: 16×16
    GpuStaticBuffer skirt_vbo;         // TerrainVertex * TERRAIN_SKIRT_VERTS (Item 7)
    GpuStaticBuffer skirt_ibo;         // uint16_t * TERRAIN_SKIRT_IDX
    NavMesh          navmesh;
    TerrainHeightmap heightmap;         // CPU copy for height queries
    TerrainPassGrid  pass_grid;         // L2-inspired O(1) passability bitmask
    ChunkPropInstance props[CHUNK_MAX_PROPS];
    int              prop_count = 0;    // valid entries in props[]
    // GroundTexLayer indices: 0=base,1=slope,2=cliff (per biome), 3=grass,4=dirt,5=road (global)
    float            ground_layers[6] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f};
    // Procedural biome crossfade targets: xyz=slot A (base/slope/cliff of the
    // FIRST grid-adjacent neighbour chunk whose resolved biome differs),
    // w=slot B (base index only of the SECOND differing neighbour, for
    // chunks at a 3-way zone corner -- one blended boundary isn't enough
    // there, confirmed visually). Equal to ground_layers[0]/this chunk's own
    // base when no such neighbour exists -- blend weight is then always 0
    // regardless of the mask texture, so a stray nonzero sample still blends
    // into itself (a no-op) rather than showing a wrong texture. See
    // ARCHITECTURE note in terrain_gen.cpp's biome-assignment block for the
    // real-Kenshi (blendmap.png/blendinfo.dat) precedent this mirrors.
    float            blend_layers[4] = {0.f, 1.f, 2.f, 0.f};
    bool             loaded = false;
    // Set true at the end of TerrainGen_Build — independent of `loaded` (which
    // means "GPU buffers uploaded", set by TerrainGen_Upload). Lets callers
    // that only need CPU heightmap data (e.g. the editor's full-world compact
    // VBO bake) work correctly even for chunks that were never GPU-uploaded —
    // see editor_world_3d_sdlgpu.cpp's windowed lazy-upload (GpuUploadBatch
    // doc comment, gpu_hal.h, explains why eager per-chunk GPU upload for all
    // 4096 chunks is not viable).
    bool             heightmap_ready = false;

    // KEN-CLUTTER Tier 2: dense ground clutter (pebbles/small rocks/small plants)
    // baked into ONE static mesh per chunk by ClutterGen_Build/Upload — one draw
    // call regardless of instance count (mirrors Kenshi's Forests::BatchedGeometry).
    GpuStaticBuffer  clutter_vbo;
    GpuStaticBuffer  clutter_ibo;
    int              clutter_index_count = 0;
    bool             clutter_loaded = false;

    // Sample height at local chunk coords (0..CHUNK_SIZE).
    // Bilinear interpolation between grid cells.
    float SampleHeight(float lx, float lz) const;
};

// Per-chunk terrain generation parameters
struct TerrainGenParams {
    float base_scale    = 0.008f;  // noise frequency (lower = larger features)
    float amplitude     = 18.0f;   // max height in metres
    int   octaves       = 6;
    float persistence   = 0.50f;
    float lacunarity    = 2.00f;
    float sea_level     = 0.0f;    // heights below this → flatten to 0
    int   seed          = 42;
    // NavMesh Recast parameters — larger cs = faster but lower quality
    float nav_cs        = 0.5f;    // cell size (m); 0.3=precise, 1.0=fast/test
    float nav_ch        = 0.2f;    // cell height (m)
    // World-space origin offset — shifts all vertex positions without moving noise.
    float world_offset_x = 0.f;
    float world_offset_z = 0.f;
    // Optional Kenshi heightmap file (.r32).  When set, noise is replaced by
    // sampled heights from the file.  amplitude still scales the result.
    const char* heightmap_r32 = nullptr;

    // Per-zone Kenshi heightmaps (overrides heightmap_r32 when zone_origin_x >= 0).
    // Each chunk (coord.x, coord.z) loads zone_{zone_origin_x+coord.x}_{zone_origin_z+coord.z}.r32.
    // Heights are stored in metres; amplitude is ignored.
    int zone_origin_x = -1;   // Kenshi zone X for chunk coord(0,0)
    int zone_origin_z = -1;   // Kenshi zone Z for chunk coord(0,0)

    // Procedural open-world parameters (defaults = disabled, preserves existing behaviour).
    float domain_warp_strength = 0.f;   // 0=disabled; 80.f for open world
    float ridge_weight         = 0.f;   // 0=disabled; 0.12f for open world
    float redistribution_power = 1.f;   // 1=no change; 3.0f for open world
    bool  force_noise          = false; // true = ignore atlas/r32, always use noise

    // Biome + faction (PROC-2). biome_scale controls region size in world-space.
    float biome_scale   = 0.f;   // 0=disabled; 0.00025f for open world (~4km regions)
    float faction_scale = 0.f;   // unused at gen time; kept for future query API
    int   num_factions  = 0;     // 0=disabled; 8 for open world

    // Targets world_hmap.r32 stats: avg≈17m, σ≈22m, 95.9% below 60m, max≈280m.
    static TerrainGenParams ProcWorld(int seed = 42) {
        TerrainGenParams p;
        p.seed                 = seed;
        p.base_scale           = 0.004f;   // 250m per wavelength — Kenshi dune scale
        p.amplitude            = 280.f;    // matches Kenshi max ~280m
        p.octaves              = 6;
        p.persistence          = 0.50f;
        p.lacunarity           = 2.00f;
        p.domain_warp_strength = 15.f;
        p.ridge_weight         = 0.08f;
        p.redistribution_power = 6.5f;
        p.force_noise          = true;
        p.biome_scale          = 0.00025f; // ~4km biome regions
        p.num_factions         = 8;
        return p;
    }
};
