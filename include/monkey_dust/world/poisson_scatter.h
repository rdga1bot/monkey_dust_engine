#pragma once
#include <cstdint>

// Bridson fast Poisson Disk Sampling for terrain prop placement.
// Guarantees minimum separation `min_dist` between any two placed objects.
// Replaces LCG scatter_props which has no distance guarantee and produces clusters.
//
// TerrainQuery::Get() must be initialised before calling PoissonScatter.
// Call only at startup or zone transition — uses malloc internally.

struct PoissonScatterParams {
    float    range;          // scatter radius from world origin (metres)
    float    min_dist;       // minimum separation between placed objects
    float    max_slope_deg;  // reject if terrain slope > this (0 = accept any)
    float    embed;          // sink object into ground by this amount (metres)
    int      max_n;          // hard cap on output count
    uint32_t seed;           // deterministic RNG seed
};

// Fills pos_out[0..N-1] with (x,y,z) world positions, returns N placed.
// pos_out must have room for max_n * 3 floats.
int PoissonScatter(float* pos_out, const PoissonScatterParams& p);
