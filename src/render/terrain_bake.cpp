#include <monkey_dust/render/terrain_bake.h>
#include <algorithm>
#include <cmath>

int TerrainBake_VertexCount(int N) {
    return (N + 1) * (N + 1) + 4 * (N + 1);
}

int TerrainBake_IndexCount(int N) {
    return N * N * 6 + 4 * N * 6;
}

namespace {

float FineHeight(float origin_x, float origin_z, float patch_size,
                  TerrainHeightSampleFn sample, float u, float v) {
    float wx = origin_x + u * patch_size;
    float wz = origin_z + v * patch_size;
    return sample(wx, wz);
}

// Bilinear interpolation of the coarse_n-quads/edge grid's 4 surrounding
// corners at local (u,v) -- see terrain_bake.h's height_coarse doc comment.
float CoarseHeight(float origin_x, float origin_z, float patch_size,
                    TerrainHeightSampleFn sample, int coarse_n, float u, float v) {
    float step = 1.0f / (float)coarse_n;
    float cu = u / step, cv = v / step;
    int c0u = (int)cu, c0v = (int)cv;
    int c1u = std::min(c0u + 1, coarse_n);
    int c1v = std::min(c0v + 1, coarse_n);
    float fu = cu - (float)c0u;
    float fv = cv - (float)c0v;

    auto corner = [&](int ci, int cj) {
        return FineHeight(origin_x, origin_z, patch_size, sample,
                           (float)ci * step, (float)cj * step);
    };
    float h00 = corner(c0u, c0v), h10 = corner(c1u, c0v);
    float h01 = corner(c0u, c1v), h11 = corner(c1u, c1v);
    float hx0 = h00 + (h10 - h00) * fu;
    float hx1 = h01 + (h11 - h01) * fu;
    return hx0 + (hx1 - hx0) * fv;
}

void EdgeUV(int e, int i, int N, float& u, float& v) {
    switch (e) {
        case 0: u = (float)i / (float)N; v = 0.f; break;             // north, row=0
        case 1: u = (float)i / (float)N; v = 1.f; break;             // south, row=N
        case 2: u = 0.f; v = (float)i / (float)N; break;             // west,  col=0
        default: u = 1.f; v = (float)i / (float)N; break;            // east,  col=N
    }
}

} // namespace

namespace {

// Forward-difference normal, same formula terrain_patch.vert's vertex
// shader uses live -- computed here once at bake time instead.
void ComputeNormal(float origin_x, float origin_z, float patch_size,
                    TerrainHeightSampleFn sample, float u, float v,
                    float h0, float step_m,
                    float& nx, float& ny, float& nz) {
    float step_uv = step_m / patch_size;
    float hR = FineHeight(origin_x, origin_z, patch_size, sample, u + step_uv, v);
    float hU = FineHeight(origin_x, origin_z, patch_size, sample, u, v + step_uv);
    float inv_step = 1.0f / step_m;
    float dhdx = (hR - h0) * inv_step;
    float dhdz = (hU - h0) * inv_step;
    nx = -dhdx; ny = 1.0f; nz = -dhdz;
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
    else             { nx = 0.f; ny = 1.f; nz = 0.f; }
}

} // namespace

void TerrainBake_ComputeVertices(float origin_x, float origin_z,
                                  float patch_size, int N, bool has_coarser,
                                  float normal_step_m,
                                  TerrainHeightSampleFn sample_height,
                                  TerrainBakedVertex* out) {
    int coarse_n = std::max(1, N / 2);

    auto fill = [&](int idx, float u, float v, float skirt) {
        float hf = FineHeight(origin_x, origin_z, patch_size, sample_height, u, v);
        float hc = has_coarser
            ? CoarseHeight(origin_x, origin_z, patch_size, sample_height, coarse_n, u, v)
            : hf;
        float nx, ny, nz;
        ComputeNormal(origin_x, origin_z, patch_size, sample_height, u, v, hf,
                      normal_step_m, nx, ny, nz);
        out[idx] = { u, v, hf, hc, skirt, nx, ny, nz };
    };

    for (int row = 0; row <= N; ++row) {
        for (int col = 0; col <= N; ++col) {
            int vi = row * (N + 1) + col;
            fill(vi, (float)col / (float)N, (float)row / (float)N, 0.0f);
        }
    }

    int surf_vc = (N + 1) * (N + 1);
    int base = surf_vc;
    for (int e = 0; e < 4; ++e) {
        for (int i = 0; i <= N; ++i) {
            float u, v;
            EdgeUV(e, i, N, u, v);
            fill(base + i, u, v, 1.0f);
        }
        base += (N + 1);
    }
}

void TerrainBake_ComputeIndices(int N, uint32_t* idx) {
    int ii = 0;
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            uint32_t v00 = (uint32_t)(row * (N + 1) + col);
            uint32_t v10 = v00 + 1;
            uint32_t v01 = v00 + (uint32_t)(N + 1);
            uint32_t v11 = v01 + 1;
            idx[ii++] = v00; idx[ii++] = v01; idx[ii++] = v10;
            idx[ii++] = v10; idx[ii++] = v01; idx[ii++] = v11;
        }
    }

    auto edge_surf_idx = [N](int e, int i) -> int {
        switch (e) {
            case 0: return 0 * (N + 1) + i;
            case 1: return N * (N + 1) + i;
            case 2: return i * (N + 1) + 0;
            default: return i * (N + 1) + N;
        }
    };
    int surf_vc = (N + 1) * (N + 1);
    int skirt_base = surf_vc;
    for (int e = 0; e < 4; ++e) {
        int base = skirt_base + e * (N + 1);
        for (int i = 0; i < N; ++i) {
            uint32_t s0 = (uint32_t)edge_surf_idx(e, i);
            uint32_t s1 = (uint32_t)edge_surf_idx(e, i + 1);
            uint32_t k0 = (uint32_t)(base + i);
            uint32_t k1 = (uint32_t)(base + i + 1);
            idx[ii++] = s0; idx[ii++] = k0; idx[ii++] = s1;
            idx[ii++] = s1; idx[ii++] = k0; idx[ii++] = k1;
        }
    }
}
