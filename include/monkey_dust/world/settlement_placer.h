#pragma once
// SettlementPlacer — rule-based settlement placement for procedural worlds.
// Scores candidates by terrain flatness + biome suitability; greedy selection
// with 500 m minimum gap.  No malloc; no STL; C++17.

#include <monkey_dust/world/biome_system.h>
#include <cstdint>

static constexpr int SP_MAX_SETTLEMENTS = 128;

struct SettlementRecord {
    float     x, z;           // world-space centre (metres)
    int       grid_x, grid_z; // atlas zone coords [0..63]
    float     score;          // placement quality [0..1]
    int       faction_id;     // Voronoi faction index [0..num_factions)
    BiomeType biome;
};

class SettlementPlacer {
public:
    // Place up to min(count, SP_MAX_SETTLEMENTS) settlements.
    // biome_grid: flat row-major uint8_t[64*64] of BiomeType cast values;
    //             pass nullptr to skip biome scoring (all biomes equal).
    // Requires TerrainAtlas_Loaded() — returns 0 if atlas not loaded.
    int Place(int seed, int count, int num_factions,
              const uint8_t* biome_grid,
              SettlementRecord* out, int max_out);
};
