#include <monkey_dust/world/poisson_scatter.h>
#include <monkey_dust/world/terrain_query.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>

// Bridson (2007) fast Poisson Disk Sampling — O(n), k=30 attempts per active sample.
// Domain: square [-range, +range]² centred on world origin.
// Grid cell = min_dist/√2 ensures at most one accepted point per cell.
int PoissonScatter(float* pos_out, const PoissonScatterParams& par)
{
    static constexpr int   K       = 30;
    static constexpr float INV_SQ2 = 0.70710678f;
    static constexpr float DEG2R   = 0.01745329f;
    static constexpr float TAU     = 6.28318530f;

    const float r    = par.min_dist;
    const float cell = r * INV_SQ2;
    const float D    = par.range * 2.f;
    const int   W    = (int)(D / cell) + 2;

    int*   grid   = (int*)  malloc(sizeof(int)   * (size_t)(W * W));
    float* xz     = (float*)malloc(sizeof(float) * 2u * (size_t)par.max_n);
    int*   active = (int*)  malloc(sizeof(int)   * (size_t)par.max_n);
    if (!grid || !xz || !active) {
        free(grid); free(xz); free(active);
        fprintf(stderr, "[PoissonScatter] alloc failed (W=%d max_n=%d)\n", W, par.max_n);
        return 0;
    }
    memset(grid, -1, sizeof(int) * (size_t)(W * W));

    auto& tq = TerrainQuery::Get();

    uint32_t lcg = par.seed;
    auto rnd = [&]() -> float {
        lcg = lcg * 1664525u + 1013904223u;
        return (float)(lcg & 0xFFFFu) / 65535.f;
    };

    // Grid helpers: clamp world (x,z) → grid cell index
    auto cell_of = [&](float x, float z) -> int {
        int gx = (int)((x + par.range) / cell);
        int gz = (int)((z + par.range) / cell);
        gx = gx < 0 ? 0 : (gx >= W ? W-1 : gx);
        gz = gz < 0 ? 0 : (gz >= W ? W-1 : gz);
        return gz * W + gx;
    };

    // Check if any existing point within r of (qx, qz)
    auto too_close = [&](float qx, float qz) -> bool {
        int gx = (int)((qx + par.range) / cell);
        int gz = (int)((qz + par.range) / cell);
        const float r2 = r * r;
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                int nx_ = gx+dx, nz_ = gz+dz;
                if (nx_ < 0 || nx_ >= W || nz_ < 0 || nz_ >= W) continue;
                int ni = grid[nz_ * W + nx_];
                if (ni < 0) continue;
                float ex = xz[ni*2+0] - qx, ez = xz[ni*2+1] - qz;
                if (ex*ex + ez*ez < r2) return true;
            }
        }
        return false;
    };

    // Declare counters before any lambda that captures them by ref
    int n_pts    = 0;
    int n_active = 0;

    // Register accepted point: writes pos_out, xz[], grid[], active[]
    auto accept_pt = [&](float qx, float qz) {
        float qy = tq.GetHeight(qx, qz) - par.embed;
        pos_out[n_pts*3+0] = qx;
        pos_out[n_pts*3+1] = qy;
        pos_out[n_pts*3+2] = qz;
        xz[n_pts*2+0] = qx;
        xz[n_pts*2+1] = qz;
        grid[cell_of(qx, qz)] = n_pts;
        active[n_active++] = n_pts++;
    };

    // Seed: find a valid first point
    {
        bool seeded = false;
        for (int t = 0; t < K * 4 && !seeded; ++t) {
            float sx = (rnd()*2.f - 1.f) * par.range;
            float sz = (rnd()*2.f - 1.f) * par.range;
            if (par.max_slope_deg > 0.f &&
                tq.GetSlope(sx, sz) > par.max_slope_deg * DEG2R) continue;
            accept_pt(sx, sz);
            seeded = true;
        }
        if (!seeded) { free(grid); free(xz); free(active); return 0; }
    }

    // Main Bridson loop
    while (n_active > 0 && n_pts < par.max_n) {
        int   ai = (int)(rnd() * (float)n_active) % n_active;
        int   pi = active[ai];
        float px = xz[pi*2+0], pz = xz[pi*2+1];

        bool found = false;
        for (int k = 0; k < K && n_pts < par.max_n; ++k) {
            float angle = rnd() * TAU;
            float dist  = r + rnd() * r;
            float qx    = px + cosf(angle) * dist;
            float qz    = pz + sinf(angle) * dist;

            if (qx < -par.range || qx > par.range ||
                qz < -par.range || qz > par.range) continue;
            if (par.max_slope_deg > 0.f &&
                tq.GetSlope(qx, qz) > par.max_slope_deg * DEG2R) continue;
            if (too_close(qx, qz)) continue;

            accept_pt(qx, qz);
            found = true;
        }

        if (!found)
            active[ai] = active[--n_active];
    }

    free(grid); free(xz); free(active);
    return n_pts;
}
