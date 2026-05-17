#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/world/chunk_def.h>
#include <cmath>
#include <cstring>

// ── Simplex noise (Stefan Gustavson / Ashima Arts — public domain) ────────────

static const int perm[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,
    142,8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,
    203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,
    220,105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,
    132,187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,
    186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,
    59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,
    70,221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180,
    // repeat
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,
    142,8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,
    203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,
    220,105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,
    132,187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,
    186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,
    59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,
    70,221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180,
};

static inline float s_grad2(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

float SimplexNoise2(float x, float y) {
    static constexpr float F2 = 0.366025403f;  // (sqrt(3)-1)/2
    static constexpr float G2 = 0.211324865f;  // (3-sqrt(3))/6
    float s = (x + y) * F2;
    int i = (int)floorf(x + s);
    int j = (int)floorf(y + s);
    float t = (i + j) * G2;
    float x0 = x - (i - t), y0 = y - (j - t);
    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }
    float x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
    float x2 = x0 - 1.0f + 2.0f * G2, y2 = y0 - 1.0f + 2.0f * G2;
    int ii = i & 255, jj = j & 255;
    float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f;
    float t0 = 0.5f - x0*x0 - y0*y0;
    if (t0 >= 0.0f) { t0 *= t0; n0 = t0*t0 * s_grad2(perm[ii + perm[jj]], x0, y0); }
    float t1 = 0.5f - x1*x1 - y1*y1;
    if (t1 >= 0.0f) { t1 *= t1; n1 = t1*t1 * s_grad2(perm[ii + i1 + perm[jj + j1]], x1, y1); }
    float t2 = 0.5f - x2*x2 - y2*y2;
    if (t2 >= 0.0f) { t2 *= t2; n2 = t2*t2 * s_grad2(perm[ii + 1 + perm[jj + 1]], x2, y2); }
    return 70.0f * (n0 + n1 + n2);  // [-1, 1]
}

float FBM2(float x, float y, int octaves, float persistence, float lacunarity) {
    float value = 0.0f, amplitude = 1.0f, max_value = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        value     += amplitude * SimplexNoise2(x, y);
        max_value += amplitude;
        amplitude *= persistence;
        x         *= lacunarity;
        y         *= lacunarity;
    }
    return value / max_value;
}

// ── Height sampling helpers ────────────────────────────────────────────────────

static float s_gen_height(int col, int row, ChunkCoord coord, const TerrainGenParams& p) {
    float wx = (coord.x * CHUNK_SIZE) + col * TERRAIN_STEP;
    float wz = (coord.z * CHUNK_SIZE) + row * TERRAIN_STEP;
    float n = FBM2(wx * p.base_scale + p.seed * 127.1f,
                   wz * p.base_scale + p.seed *  311.7f,
                   p.octaves, p.persistence, p.lacunarity);
    float h = ((n + 1.0f) * 0.5f) * p.amplitude;  // remap [-1,1] → [0, amplitude]
    return h < p.sea_level ? p.sea_level : h;
}

static inline int s_idx(int col, int row) { return row * (TERRAIN_GRID + 1) + col; }

// ── Splat weights from height ──────────────────────────────────────────────────

static void s_splat(float h, float amp, float* splat) {
    float t = (amp > 0.0f) ? (h / amp) : 0.0f;  // [0,1]
    // grass dominant at low elevation, rock mid, snow at top
    float grass = 1.0f - t * 2.0f;  if (grass < 0.0f) grass = 0.0f;
    float snow  = (t - 0.65f) * 3.0f; if (snow < 0.0f) snow = 0.0f; if (snow > 1.0f) snow = 1.0f;
    float rock  = 1.0f - grass - snow; if (rock < 0.0f) rock = 0.0f;
    float dirt  = 0.0f;
    float sum   = grass + rock + dirt + snow;
    if (sum > 0.0f) { grass /= sum; rock /= sum; dirt /= sum; snow /= sum; }
    splat[0] = grass; splat[1] = rock; splat[2] = dirt; splat[3] = snow;
}

// ── TerrainGen_Build ──────────────────────────────────────────────────────────

static TerrainVertex s_verts_buf[TERRAIN_VERTS];
static uint16_t      s_idx_buf  [TERRAIN_IDX];
// separate float[] nav positions (x,y,z per vert)
static float         s_nav_pos  [TERRAIN_VERTS * 3];
static int           s_nav_tri  [TERRAIN_IDX];   // same indices, cast to int

bool TerrainGen_Build(TerrainChunk& out, ChunkCoord coord, const TerrainGenParams& p) {
    float world_origin_x = coord.x * CHUNK_SIZE;
    float world_origin_z = coord.z * CHUNK_SIZE;

    // ── 1. Heights ────────────────────────────────────────────────────────────
    for (int row = 0; row <= TERRAIN_GRID; ++row) {
        for (int col = 0; col <= TERRAIN_GRID; ++col) {
            out.heightmap.h[s_idx(col, row)] = s_gen_height(col, row, coord, p);
        }
    }

    // ── 2. Vertices ────────────────────────────────────────────────────────────
    for (int row = 0; row <= TERRAIN_GRID; ++row) {
        for (int col = 0; col <= TERRAIN_GRID; ++col) {
            int vi = s_idx(col, row);
            float h = out.heightmap.h[vi];
            float wx = world_origin_x + col * TERRAIN_STEP;
            float wz = world_origin_z + row * TERRAIN_STEP;

            s_verts_buf[vi].x = wx;
            s_verts_buf[vi].y = h;
            s_verts_buf[vi].z = wz;

            // World-space UV for texture tiling (1m = 1 UV unit / texture repeat)
            s_verts_buf[vi].u = wx;
            s_verts_buf[vi].v = wz;

            s_splat(h, p.amplitude, s_verts_buf[vi].splat);

            // Nav positions (flat float array for Recast)
            s_nav_pos[vi * 3 + 0] = wx;
            s_nav_pos[vi * 3 + 1] = h;
            s_nav_pos[vi * 3 + 2] = wz;

            // Zero normal — filled in step 3
            s_verts_buf[vi].nx = 0.0f;
            s_verts_buf[vi].ny = 0.0f;
            s_verts_buf[vi].nz = 0.0f;
        }
    }

    // ── 3. Indices + accumulate normals ───────────────────────────────────────
    // Accumulate face normals into nx/ny/nz, then normalize per vertex.
    int ii = 0;
    for (int row = 0; row < TERRAIN_GRID; ++row) {
        for (int col = 0; col < TERRAIN_GRID; ++col) {
            uint16_t bl = (uint16_t)s_idx(col,   row);
            uint16_t br = (uint16_t)s_idx(col+1, row);
            uint16_t tl = (uint16_t)s_idx(col,   row+1);
            uint16_t tr = (uint16_t)s_idx(col+1, row+1);

            // Triangle 0: bl, br, tl
            s_idx_buf[ii+0] = bl; s_idx_buf[ii+1] = br; s_idx_buf[ii+2] = tl;
            s_nav_tri[ii+0] = bl; s_nav_tri[ii+1] = br; s_nav_tri[ii+2] = tl;

            // Triangle 1: br, tr, tl
            s_idx_buf[ii+3] = br; s_idx_buf[ii+4] = tr; s_idx_buf[ii+5] = tl;
            s_nav_tri[ii+3] = br; s_nav_tri[ii+4] = tr; s_nav_tri[ii+5] = tl;

            // Accumulate face normals for each triangle
            auto accum_normal = [&](uint16_t a, uint16_t b, uint16_t c) {
                TerrainVertex& va = s_verts_buf[a];
                TerrainVertex& vb = s_verts_buf[b];
                TerrainVertex& vc = s_verts_buf[c];
                float ex = vb.x - va.x, ey = vb.y - va.y, ez = vb.z - va.z;
                float fx = vc.x - va.x, fy = vc.y - va.y, fz = vc.z - va.z;
                float nx = ey * fz - ez * fy;
                float ny = ez * fx - ex * fz;
                float nz = ex * fy - ey * fx;
                for (uint16_t idx : {a, b, c}) {
                    s_verts_buf[idx].nx += nx;
                    s_verts_buf[idx].ny += ny;
                    s_verts_buf[idx].nz += nz;
                }
            };
            accum_normal(bl, br, tl);
            accum_normal(br, tr, tl);

            ii += 6;
        }
    }

    // Normalize accumulated normals
    for (int i = 0; i < TERRAIN_VERTS; ++i) {
        float& nx = s_verts_buf[i].nx;
        float& ny = s_verts_buf[i].ny;
        float& nz = s_verts_buf[i].nz;
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
        else { nx = 0.0f; ny = 1.0f; nz = 0.0f; }
    }

    out.coord = coord;
    out.loaded = false;  // Upload() sets this

    // ── 4. NavMesh ────────────────────────────────────────────────────────────
    bool nav_ok = out.navmesh.Build(
        s_nav_pos, TERRAIN_VERTS,
        s_nav_tri, TERRAIN_TRIS,
        0.3f, 0.2f
    );

    // Store vertex/index data for TerrainGen_Upload (static buffers remain valid
    // since Upload must be called synchronously before the next Build call).
    out.loaded = false;  // Upload sets true
    return nav_ok;
}

// CONSTRAINT: TerrainGen_Upload must be called from the render thread before
// the next TerrainGen_Build (static staging buffers are single-use).
void TerrainGen_Upload(TerrainChunk& chunk) {
    chunk.vbo.Init(0x8892u /*GL_ARRAY_BUFFER*/,
                   s_verts_buf,
                   sizeof(TerrainVertex) * TERRAIN_VERTS);
    chunk.ibo.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/,
                   s_idx_buf,
                   sizeof(uint16_t) * TERRAIN_IDX);
    chunk.loaded = true;
}

// ── TerrainChunk::SampleHeight ────────────────────────────────────────────────

float TerrainChunk::SampleHeight(float lx, float lz) const {
    // Clamp to chunk bounds
    if (lx < 0.0f) lx = 0.0f;
    if (lz < 0.0f) lz = 0.0f;
    if (lx > CHUNK_SIZE) lx = CHUNK_SIZE;
    if (lz > CHUNK_SIZE) lz = CHUNK_SIZE;

    float fc = lx / TERRAIN_STEP;
    float fr = lz / TERRAIN_STEP;
    int c0 = (int)fc, r0 = (int)fr;
    if (c0 >= TERRAIN_GRID) c0 = TERRAIN_GRID - 1;
    if (r0 >= TERRAIN_GRID) r0 = TERRAIN_GRID - 1;
    int c1 = c0 + 1, r1 = r0 + 1;

    float tx = fc - c0, tz = fr - r0;
    float h00 = heightmap.h[s_idx(c0, r0)];
    float h10 = heightmap.h[s_idx(c1, r0)];
    float h01 = heightmap.h[s_idx(c0, r1)];
    float h11 = heightmap.h[s_idx(c1, r1)];
    return h00 * (1-tx)*(1-tz) + h10 * tx*(1-tz) + h01 * (1-tx)*tz + h11 * tx*tz;
}
