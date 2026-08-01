#pragma once
#include <monkey_dust/world/chunk_def.h>
#include <monkey_dust/world/biome_def.h>

// TerrainTile -- formal per-zone tile metadata (TERRAIN_CA_REBUILD_PROMPT.md
// Phase 1: "зона-як-тайл" data model, modeled on Total War's Terry tiles --
// see TOTALWAR_ENGINE_DIGEST.md §1.1/1.5). NOT a new world format: every
// field below is already true of the existing global-atlas artifacts
// (TerrainWorldHeightmap, md_terrain.dds, ground_bake, zoneGroundLayers
// SSBO) -- this formalizes the zone-sized rectangle each already
// implicitly reserves, so future per-tile work (streaming, baked
// geometry, decal-projection shading) has one shared, named concept to
// build on instead of re-deriving origin/extent math ad hoc at each call
// site. Coordinates are ABSOLUTE world metres (P1-2, terrain_research/
// 00_PREREQUISITES.md) -- translate through TerrainQuery, never assume a
// caller's session-local space matches these directly.
//
// Scope note: this is the data-MODEL half of Phase 1 only. The offline
// "Terry pipeline" consolidation (bake_tiles.py unifying md_bake_ground_
// layers.py/md_stitch_terrain.py/kenshi2mde.py into per-tile physical
// artifacts) is deferred -- see terrain_research/perf/PROGRESS.md's
// Phase 1 entry for why: every artifact TerrainTile describes below is
// STILL one global, whole-world atlas (unchanged, zero regression risk);
// this only adds a named accessor for the rectangle each zone already
// occupies within those atlases. kenshi2mde.py --validate already checks
// zone-to-zone edge continuity across this exact partitioning (8064
// edges, PASS) -- that satisfies Phase 1's validate requirement without
// new code.
struct TerrainTile {
    int   gx = 0, gz = 0;                  // zone grid index, [0..63]
    float origin_x = 0.f, origin_z = 0.f;  // absolute world-space origin (gx*CHUNK_SIZE, gz*CHUNK_SIZE)
    float extent = CHUNK_SIZE;             // real Kenshi zone size, 460.8m -- every tile is the same size

    // Own ground-layer set for this tile -- currently backed by
    // BiomeRegistry's per-zone BiomeDef (tex_base/slope/cliff/grass/
    // dirt/road + tiling/brightness); NOT a copy, a live reference --
    // BiomeRegistry remains the single source of truth for layer data.
    const BiomeDef* layers = nullptr;

    // True world-space bounding rect this tile occupies inside every
    // global atlas (TerrainWorldHeightmap, md_terrain.dds/ground_bake,
    // zoneGroundLayers SSBO) -- all of them share this exact convention
    // (KENSHI_WORLD_SIZE = 64*CHUNK_SIZE, zone (gx,gz) at
    // [gx*CHUNK_SIZE, (gx+1)*CHUNK_SIZE) x [gz*CHUNK_SIZE, (gz+1)*CHUNK_SIZE)).
    // The "height-region"/"blend-region" language from the Phase 1 prompt
    // text refers to this SAME rectangle.
    [[nodiscard]] bool Contains(float wx, float wz) const {
        return wx >= origin_x && wx < origin_x + extent &&
               wz >= origin_z && wz < origin_z + extent;
    }
};

class TerrainTileGrid {
public:
    static constexpr int kGridN = 64;  // 64x64 zones, real Kenshi world size

    // Zone (gx,gz) -> TerrainTile. gx/gz are clamped to [0, kGridN-1] --
    // never out-of-bounds, matches BiomeRegistry::ForZone/TerrainAtlas_
    // SampleWorld's own fallback-to-nearest-valid-data convention.
    static TerrainTile ForZone(int gx, int gz);

    // Absolute world position -> owning tile. Never fails (clamps to the
    // nearest edge zone) -- callers must still translate through
    // TerrainQuery first if wx/wz came from a session-local space (P1-2).
    static TerrainTile ForWorldPos(float wx, float wz);
};
