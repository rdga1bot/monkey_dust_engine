#include <monkey_dust/world/settlement_placer.h>
#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/world/chunk_def.h>

static constexpr int   kAtlasZones = 64;
static constexpr int   kAtlasVerts = 65;
static constexpr float kMinDist2   = 500.f * 500.f;
static constexpr float kFlatR      = 50.f;
static constexpr int   kMaxCands   = 512;

static float s_height_at(float wx, float wz) {
    int zx = (int)(wx / CHUNK_SIZE);
    int zy = (int)(wz / CHUNK_SIZE);
    if (zx < 0 || zx >= kAtlasZones || zy < 0 || zy >= kAtlasZones) return 0.f;
    float lx = wx - zx * CHUNK_SIZE;
    float lz = wz - zy * CHUNK_SIZE;
    int col = (int)(lx * (kAtlasVerts - 1) / CHUNK_SIZE + 0.5f);
    int row = (int)(lz * (kAtlasVerts - 1) / CHUNK_SIZE + 0.5f);
    if (col >= kAtlasVerts) col = kAtlasVerts - 1;
    if (row >= kAtlasVerts) row = kAtlasVerts - 1;
    return TerrainAtlas_GetHeight(zx, zy, col, row);
}

static float s_flatness(float cx, float cz) {
    static constexpr int   kN    = 5;
    static constexpr float kStep = kFlatR * 2.f / (kN - 1);
    float sum = 0.f, sum2 = 0.f;
    float ox = cx - kFlatR, oz = cz - kFlatR;
    for (int r = 0; r < kN; ++r)
        for (int c = 0; c < kN; ++c) {
            float h = s_height_at(ox + c * kStep, oz + r * kStep);
            sum += h; sum2 += h * h;
        }
    float inv  = 1.f / (kN * kN);
    float mean = sum * inv;
    float var  = sum2 * inv - mean * mean;
    float norm = var / (150.f * 150.f);  // 150 m height range → worst case variance
    return 1.f - (norm < 1.f ? norm : 1.f);
}

static float s_biome_weight(BiomeType b) {
    switch (b) {
        case BiomeType::Grassland: return 1.0f;
        case BiomeType::Swamp:     return 0.5f;
        case BiomeType::Badlands:  return 0.4f;
        case BiomeType::Tundra:    return 0.3f;
        case BiomeType::Desert:    return 0.2f;
        case BiomeType::Rocky:     return 0.1f;
        default:                   return 0.2f;
    }
}

struct Cand {
    float     x, z;
    float     score;
    int       gx, gz;
    BiomeType biome;
};

int SettlementPlacer::Place(int seed, int count, int num_factions,
                             const uint8_t* biome_grid,
                             SettlementRecord* out, int max_out) {
    if (!TerrainAtlas_Loaded()) return 0;
    if (count > SP_MAX_SETTLEMENTS) count = SP_MAX_SETTLEMENTS;
    if (max_out < count)            count = max_out;
    if (count <= 0)                 return 0;

    // ── 1. Stratified candidate generation ───────────────────────────────────
    int cand_target = count * 4 < kMaxCands ? count * 4 : kMaxCands;
    int cells = 1;
    while (cells * cells < cand_target) ++cells;
    float cell_sz = (float)kAtlasZones / cells;

    Cand  cands[kMaxCands];
    int   ncands = 0;

    unsigned int s = (unsigned int)seed ^ 0xA5B3C7D1u;
    auto lcg = [&]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / (float)(1 << 24);
    };

    for (int ry = 0; ry < cells && ncands < kMaxCands; ++ry) {
        for (int rx = 0; rx < cells && ncands < kMaxCands; ++rx) {
            int gx = (int)(rx * cell_sz + lcg() * cell_sz);
            int gz = (int)(ry * cell_sz + lcg() * cell_sz);
            if (gx >= kAtlasZones) gx = kAtlasZones - 1;
            if (gz >= kAtlasZones) gz = kAtlasZones - 1;

            float cx = (gx + 0.5f) * CHUNK_SIZE;
            float cz = (gz + 0.5f) * CHUNK_SIZE;

            BiomeType btype = BiomeType::Grassland;
            if (biome_grid)
                btype = (BiomeType)biome_grid[gz * kAtlasZones + gx];

            float flat  = s_flatness(cx, cz);
            float bw    = s_biome_weight(btype);
            cands[ncands++] = { cx, cz, flat * 0.7f + bw * 0.3f, gx, gz, btype };
        }
    }

    // ── 2. Insertion sort descending by score ─────────────────────────────────
    for (int i = 1; i < ncands; ++i) {
        Cand key = cands[i];
        int  j   = i - 1;
        while (j >= 0 && cands[j].score < key.score) {
            cands[j + 1] = cands[j];
            --j;
        }
        cands[j + 1] = key;
    }

    // ── 3. Greedy selection + Voronoi faction + output ────────────────────────
    FactionVoronoi voronoi;
    int nf = num_factions > 0 ? num_factions : 8;
    voronoi.Init(nf, seed, kAtlasZones * CHUNK_SIZE, kAtlasZones * CHUNK_SIZE);

    float sel_x[SP_MAX_SETTLEMENTS];
    float sel_z[SP_MAX_SETTLEMENTS];
    int   nsel = 0;

    for (int i = 0; i < ncands && nsel < count; ++i) {
        bool too_close = false;
        for (int k = 0; k < nsel; ++k) {
            float dx = cands[i].x - sel_x[k];
            float dz = cands[i].z - sel_z[k];
            if (dx*dx + dz*dz < kMinDist2) { too_close = true; break; }
        }
        if (too_close) continue;

        sel_x[nsel] = cands[i].x;
        sel_z[nsel] = cands[i].z;

        out[nsel].x          = cands[i].x;
        out[nsel].z          = cands[i].z;
        out[nsel].grid_x     = cands[i].gx;
        out[nsel].grid_z     = cands[i].gz;
        out[nsel].score      = cands[i].score;
        out[nsel].faction_id = voronoi.NearestFaction(cands[i].x, cands[i].z);
        out[nsel].biome      = cands[i].biome;
        ++nsel;
    }
    return nsel;
}
