#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/world/chunk_def.h>
#include <monkey_dust/world/biome_system.h>
#include <monkey_dust/world/biome_def.h>
#include <monkey_dust/world/world_registry.h>
#include <monkey_dust/world/terrain_pass_grid.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <climits>

// ── Terrain Atlas ─────────────────────────────────────────────────────────────
static constexpr uint32_t ATLAS_MAGIC  = 0x414D4800u;
static constexpr int      ATLAS_ZONES  = 64;
static constexpr int      ATLAS_VERTS  = 65;           // TERRAIN_GRID+1
static constexpr int      ATLAS_ZBLOCK = ATLAS_VERTS * ATLAS_VERTS; // 4225

static float   s_atlas_h   [ATLAS_ZONES * ATLAS_ZONES * ATLAS_ZBLOCK]; // ~66 MB BSS
static float   s_atlas_hmin[ATLAS_ZONES * ATLAS_ZONES];
static float   s_atlas_hmax[ATLAS_ZONES * ATLAS_ZONES];
static uint8_t s_atlas_dirty[ATLAS_ZONES * ATLAS_ZONES];
static bool    s_atlas_loaded = false;
static char    s_atlas_path[512] = {};

static int s_atlas_zi(int zx, int zy)          { return zy * ATLAS_ZONES + zx; }
static int s_atlas_hi(int zx, int zy, int c, int r) {
    return s_atlas_zi(zx, zy) * ATLAS_ZBLOCK + r * ATLAS_VERTS + c;
}

bool TerrainAtlas_Load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint32_t magic, zx, zy, verts;
    if (fread(&magic, 4, 1, f) != 1 || magic != ATLAS_MAGIC ||
        fread(&zx,    4, 1, f) != 1 || fread(&zy,    4, 1, f) != 1 ||
        fread(&verts, 4, 1, f) != 1 ||
        (int)zx != ATLAS_ZONES || (int)zy != ATLAS_ZONES || (int)verts != ATLAS_VERTS) {
        fclose(f); return false;
    }
    for (int i = 0; i < ATLAS_ZONES * ATLAS_ZONES; ++i) {
        fread(&s_atlas_hmin[i], 4, 1, f);
        fread(&s_atlas_hmax[i], 4, 1, f);
        fread(&s_atlas_h[i * ATLAS_ZBLOCK], 4, ATLAS_ZBLOCK, f);
    }
    memset(s_atlas_dirty, 0, sizeof(s_atlas_dirty));
    s_atlas_loaded = true;
    strncpy(s_atlas_path, path, sizeof(s_atlas_path) - 1);
    fclose(f);
    fprintf(stdout, "[TerrainAtlas] loaded %s\n", path);
    return true;
}

bool TerrainAtlas_Loaded() { return s_atlas_loaded; }

float TerrainAtlas_GetHeight(int zx, int zy, int col, int row) {
    if (!s_atlas_loaded || zx < 0 || zx >= ATLAS_ZONES ||
        zy < 0 || zy >= ATLAS_ZONES) return 0.f;
    return s_atlas_h[s_atlas_hi(zx, zy, col, row)];
}

void TerrainAtlas_SetHeight(int zx, int zy, int col, int row, float h) {
    if (!s_atlas_loaded || zx < 0 || zx >= ATLAS_ZONES ||
        zy < 0 || zy >= ATLAS_ZONES) return;
    int idx = s_atlas_hi(zx, zy, col, row);
    s_atlas_h[idx] = h;
    int zi = s_atlas_zi(zx, zy);
    s_atlas_dirty[zi] = 1;
    if (h < s_atlas_hmin[zi]) s_atlas_hmin[zi] = h;
    if (h > s_atlas_hmax[zi]) s_atlas_hmax[zi] = h;
}

bool TerrainAtlas_ZoneDirty(int zx, int zy) {
    if (zx < 0 || zx >= ATLAS_ZONES || zy < 0 || zy >= ATLAS_ZONES) return false;
    return s_atlas_dirty[s_atlas_zi(zx, zy)] != 0;
}

bool TerrainAtlas_Save(const char* path) {
    if (!s_atlas_loaded) return false;
    FILE* f = fopen(path, "r+b");
    if (!f) return false;
    int saved = 0;
    for (int i = 0; i < ATLAS_ZONES * ATLAS_ZONES; ++i) {
        if (!s_atlas_dirty[i]) continue;
        long off = 16L + (long)i * (8L + ATLAS_ZBLOCK * 4L);
        fseek(f, off, SEEK_SET);
        fwrite(&s_atlas_hmin[i], 4, 1, f);
        fwrite(&s_atlas_hmax[i], 4, 1, f);
        fwrite(&s_atlas_h[i * ATLAS_ZBLOCK], 4, ATLAS_ZBLOCK, f);
        s_atlas_dirty[i] = 0;
        ++saved;
    }
    fclose(f);
    fprintf(stdout, "[TerrainAtlas] saved %d dirty zone(s)\n", saved);
    return true;
}

// SaveEdits/LoadEdits/HasEdits — thin wrappers over the r32 atlas API
// (editor brush tool calls these; for r32 backend Save writes directly to atlas file)
bool TerrainAtlas_SaveEdits(const char* path) { return TerrainAtlas_Save(path); }
bool TerrainAtlas_LoadEdits(const char*)       { return s_atlas_loaded; } // already loaded by TerrainAtlas_Load
bool TerrainAtlas_HasEdits() {
    for (int i = 0; i < ATLAS_ZONES * ATLAS_ZONES; ++i)
        if (s_atlas_dirty[i]) return true;
    return false;
}

void TerrainAtlas_SmoothBoundaries() {
    if (!s_atlas_loaded) return;
    // Kenshi fullmap zone pixel boundaries can have >20m height jumps in one
    // 7.8m terrain step (71° cliff faces → NdotL=0 → sharp dark seams).
    // Blend 15 vertices on each side toward the shared boundary vertex.
    // Kernel: h[k] = lerp(h_bnd, h[k], k/(N+1)) for k=1..N.
    // k=1 → 94% boundary; k=15 → 6% boundary.
    // N=15: worst 71° cliff (22.9m/7.8m step) → ≤10° per step, NdotL≥0.65 → indistinguishable from flat.
    constexpr int N = 15;

    // X-direction seams: average col=64 of zone(zx) with col=0 of zone(zx+1),
    // then smooth N interior verts on each side toward that shared average.
    // This ensures the shared boundary vertex is identical in both chunks.
    for (int zy = 0; zy < ATLAS_ZONES; ++zy) {
        for (int zx = 0; zx < ATLAS_ZONES - 1; ++zx) {
            for (int row = 0; row < ATLAS_VERTS; ++row) {
                float& h_left  = s_atlas_h[s_atlas_hi(zx,   zy, ATLAS_VERTS-1, row)]; // col=64 of A
                float& h_right = s_atlas_h[s_atlas_hi(zx+1, zy, 0,             row)]; // col=0  of B
                float h_bnd = (h_left + h_right) * 0.5f;  // average → shared vertex
                h_left  = h_bnd;
                h_right = h_bnd;
                for (int k = 1; k <= N; ++k) {
                    float t = (float)k / (N + 1);
                    float& hr = s_atlas_h[s_atlas_hi(zx+1, zy, k,             row)];
                    hr = (1.f-t)*h_bnd + t*hr;
                    float& hl = s_atlas_h[s_atlas_hi(zx,   zy, ATLAS_VERTS-1-k, row)];
                    hl = (1.f-t)*h_bnd + t*hl;
                }
            }
        }
    }

    // Z-direction seams: average row=64 of zone(zy) with row=0 of zone(zy+1).
    for (int zy = 0; zy < ATLAS_ZONES - 1; ++zy) {
        for (int zx = 0; zx < ATLAS_ZONES; ++zx) {
            for (int col = 0; col < ATLAS_VERTS; ++col) {
                float& h_bot = s_atlas_h[s_atlas_hi(zx, zy,   col, ATLAS_VERTS-1)]; // row=64 of A
                float& h_top = s_atlas_h[s_atlas_hi(zx, zy+1, col, 0            )]; // row=0  of B
                float h_bnd = (h_bot + h_top) * 0.5f;
                h_bot = h_bnd;
                h_top = h_bnd;
                for (int k = 1; k <= N; ++k) {
                    float t = (float)k / (N + 1);
                    float& ht = s_atlas_h[s_atlas_hi(zx, zy+1, col, k            )];
                    ht = (1.f-t)*h_bnd + t*ht;
                    float& hb = s_atlas_h[s_atlas_hi(zx, zy,   col, ATLAS_VERTS-1-k)];
                    hb = (1.f-t)*h_bnd + t*hb;
                }
            }
        }
    }
}

void TerrainAtlas_StitchEdge(int zx, int zy, int dir) {
    if (!s_atlas_loaded) return;
    if (zx < 0 || zy < 0 || zx >= ATLAS_ZONES || zy >= ATLAS_ZONES) return;
    constexpr int N = 15;
    if (dir == 0 && zx < ATLAS_ZONES - 1) {
        for (int row = 0; row < ATLAS_VERTS; ++row) {
            float& h_left  = s_atlas_h[s_atlas_hi(zx,   zy, ATLAS_VERTS-1, row)];
            float& h_right = s_atlas_h[s_atlas_hi(zx+1, zy, 0,             row)];
            float h_bnd = (h_left + h_right) * 0.5f;
            h_left = h_bnd; h_right = h_bnd;
            for (int k = 1; k <= N; ++k) {
                float t = (float)k / (N + 1);
                float& hr = s_atlas_h[s_atlas_hi(zx+1, zy, k,             row)];
                hr = (1.f - t) * h_bnd + t * hr;
                float& hl = s_atlas_h[s_atlas_hi(zx,   zy, ATLAS_VERTS-1-k, row)];
                hl = (1.f - t) * h_bnd + t * hl;
            }
        }
    } else if (dir == 1 && zy < ATLAS_ZONES - 1) {
        for (int col = 0; col < ATLAS_VERTS; ++col) {
            float& h_bot = s_atlas_h[s_atlas_hi(zx, zy,   col, ATLAS_VERTS-1)];
            float& h_top = s_atlas_h[s_atlas_hi(zx, zy+1, col, 0            )];
            float h_bnd = (h_bot + h_top) * 0.5f;
            h_bot = h_bnd; h_top = h_bnd;
            for (int k = 1; k <= N; ++k) {
                float t = (float)k / (N + 1);
                float& ht = s_atlas_h[s_atlas_hi(zx, zy+1, col, k)];
                ht = (1.f - t) * h_bnd + t * ht;
                float& hb = s_atlas_h[s_atlas_hi(zx, zy,   col, ATLAS_VERTS-1-k)];
                hb = (1.f - t) * h_bnd + t * hb;
            }
        }
    }
}

// forward declaration — definition is after s_load_r32 below
static float s_hmap_sample(const float* hmap, int hmap_w, int hmap_h, float u, float v);

// ── Master heightmap (macro geography, loaded once at startup) ────────────────
// Loaded via TerrainMaster_Load(); sampled in s_gen_height() when available.
// Format: uint32 w, uint32 h, float hmin, float hmax, then w*h float32 heights.
// Covers [0, ATLAS_ZONES*CHUNK_SIZE] × [0, ATLAS_ZONES*CHUNK_SIZE] world space.

static float* s_master_h    = nullptr;
static int    s_master_w    = 0;
static int    s_master_hh   = 0;   // height (rows) — avoid clash with s_master_h ptr name
static float  s_master_wext = 1.f; // world extent X (metres) — set in TerrainMaster_Load
static float  s_master_zext = 1.f; // world extent Z (metres)
static float  s_master_hmax = 0.f; // stored hmax from file header

bool TerrainMaster_Load(const char* path, float world_extent_x, float world_extent_z) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    unsigned int w = 0, h = 0;
    float hmin = 0.f, hmax = 0.f;
    if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 ||
        fread(&hmin, 4, 1, f) != 1 || fread(&hmax, 4, 1, f) != 1 ||
        w == 0 || h == 0) {
        fclose(f); return false;
    }
    delete[] s_master_h;
    s_master_h    = new float[w * h];
    s_master_w    = (int)w;
    s_master_hh   = (int)h;
    s_master_wext = world_extent_x > 0.f ? world_extent_x : 1.f;
    s_master_zext = world_extent_z > 0.f ? world_extent_z : 1.f;
    s_master_hmax = hmax;
    bool ok = fread(s_master_h, sizeof(float), w * h, f) == w * h;
    fclose(f);
    if (!ok) { delete[] s_master_h; s_master_h = nullptr; return false; }
    fprintf(stdout, "[TerrainMaster] loaded %s (%u×%u, hmax=%.1fm)\n",
            path, w, h, hmax);
    return true;
}

bool TerrainMaster_Loaded() { return s_master_h != nullptr; }

float TerrainMaster_SampleWorld(float wx, float wz) {
    if (!s_master_h) return 0.f;
    float u = wx / s_master_wext;
    float v = wz / s_master_zext;
    u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
    v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    return s_hmap_sample(s_master_h, s_master_w, s_master_hh, u, v);
}

float TerrainMaster_GetPixel(int col, int row) {
    if (!s_master_h) return 0.f;
    col = col < 0 ? 0 : (col >= s_master_w  ? s_master_w  - 1 : col);
    row = row < 0 ? 0 : (row >= s_master_hh ? s_master_hh - 1 : row);
    return s_master_h[row * s_master_w + col];
}

void TerrainMaster_SetPixel(int col, int row, float h) {
    if (!s_master_h) return;
    if (col < 0 || col >= s_master_w || row < 0 || row >= s_master_hh) return;
    s_master_h[row * s_master_w + col] = h;
}

int   TerrainMaster_Width()  { return s_master_w; }
int   TerrainMaster_Height() { return s_master_hh; }
float TerrainMaster_HMax()   { return s_master_hmax; }

bool TerrainMaster_Save(const char* path) {
    if (!s_master_h) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    unsigned int w = (unsigned int)s_master_w;
    unsigned int h = (unsigned int)s_master_hh;
    float hmin = s_master_h[0], hmax = s_master_h[0];
    int n = s_master_w * s_master_hh;
    for (int i = 1; i < n; ++i) {
        if (s_master_h[i] < hmin) hmin = s_master_h[i];
        if (s_master_h[i] > hmax) hmax = s_master_h[i];
    }
    s_master_hmax = hmax;
    bool ok = fwrite(&w, 4, 1, f) == 1 &&
              fwrite(&h, 4, 1, f) == 1 &&
              fwrite(&hmin, 4, 1, f) == 1 &&
              fwrite(&hmax, 4, 1, f) == 1 &&
              fwrite(s_master_h, sizeof(float), n, f) == (size_t)n;
    fclose(f);
    return ok;
}

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
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
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

    // ── Master heightmap: macro geography base layer ──────────────────────────
    // When loaded, provides the large-scale structure (mountain ranges, valleys).
    // Noise adds surface detail on top; amplitude is scaled down accordingly.
    float h_macro = 0.f;
    float detail_scale = 1.f;   // fraction of amplitude used for noise detail
    if (s_master_h) {
        float u = (wx + p.world_offset_x) / s_master_wext;
        float v = (wz + p.world_offset_z) / s_master_zext;
        u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
        v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        h_macro = s_hmap_sample(s_master_h, s_master_w, s_master_hh, u, v);
        detail_scale = 0.18f;   // noise adds ±18% of amplitude as surface detail
    }

    // ── Noise detail layer ────────────────────────────────────────────────────
    float sx = wx * p.base_scale + p.seed * 127.1f;
    float sz = wz * p.base_scale + p.seed * 311.7f;

    if (p.domain_warp_strength > 0.f) {
        float ws = p.base_scale * p.domain_warp_strength;
        sx += SimplexNoise2(sx + 1.7f, sz + 9.2f) * ws;
        sz += SimplexNoise2(sx + 8.3f, sz + 2.8f) * ws;
    }

    float base = (FBM2(sx, sz, p.octaves, p.persistence, p.lacunarity) + 1.f) * 0.5f;

    if (p.redistribution_power != 1.f)
        base = powf(base, p.redistribution_power);

    float h = h_macro + base * p.amplitude * detail_scale;

    if (p.ridge_weight > 0.f) {
        // Ridged multifractal: 3 octaves, signal-dependent weighting.
        // Each octave is (1-|s|)² weighted by the previous octave's value
        // → high-freq detail appears only near ridge peaks, not in valleys.
        // Produces irregular, jagged ridges instead of smooth mathematical triangles.
        float rx = wx * p.base_scale * 0.5f + p.seed * 73.1f;
        float rz = wz * p.base_scale * 0.5f + p.seed * 149.3f;
        float ridge = 0.f, ra = 0.625f, prev = 1.f;
        for (int ro = 0; ro < 3; ++ro) {
            float s = 1.f - fabsf(SimplexNoise2(rx, rz));
            s = s * s * prev;
            ridge += s * ra;
            prev  =  s;
            ra   *= 0.5f;
            rx   *= 2.17f;  // slightly off 2.0 → breaks repetitive symmetry
            rz   *= 2.17f;
        }
        h += ridge * p.amplitude * p.ridge_weight;
    }

    return h < p.sea_level ? p.sea_level : h;
}

static inline int s_idx(int col, int row) { return row * (TERRAIN_GRID + 1) + col; }

// ── Splat weights from height ──────────────────────────────────────────────────

// splat[0]=grass  splat[1]=rock  splat[2]=dirt  splat[3]=snow
static void s_splat(float h, float amp, float* splat, BiomeType biome) {
    float t = (amp > 0.0f) ? (h / amp) : 0.0f;  // [0,1] height fraction
    float grass = 0.f, rock = 0.f, dirt = 0.f, snow = 0.f;

    switch (biome) {
    case BiomeType::Desert:
        dirt  = 0.75f - t * 0.3f;  if (dirt  < 0.f) dirt  = 0.f;
        rock  = 0.20f + t * 0.6f;  if (rock  > 1.f) rock  = 1.f;
        grass = 0.05f;
        break;
    case BiomeType::Badlands:
        rock  = 0.50f + t * 0.4f;  if (rock  > 1.f) rock  = 1.f;
        dirt  = 0.35f - t * 0.2f;  if (dirt  < 0.f) dirt  = 0.f;
        grass = 0.15f * (1.f - t);
        break;
    case BiomeType::Grassland:
        grass = 0.70f - t * 0.8f;  if (grass < 0.f) grass = 0.f;
        dirt  = 0.20f;
        rock  = 0.10f + t * 0.7f;  if (rock  > 1.f) rock  = 1.f;
        break;
    case BiomeType::Rocky:
        rock  = 0.80f + t * 0.2f;  if (rock  > 1.f) rock  = 1.f;
        dirt  = 0.15f * (1.f - t);
        grass = 0.05f * (1.f - t);
        break;
    case BiomeType::Swamp:
        grass = 0.55f - t * 0.4f;  if (grass < 0.f) grass = 0.f;
        dirt  = 0.35f + t * 0.1f;
        rock  = 0.10f + t * 0.3f;  if (rock  > 1.f) rock  = 1.f;
        break;
    case BiomeType::Tundra:
        snow  = 0.50f + t * 0.4f;  if (snow  > 1.f) snow  = 1.f;
        rock  = 0.30f + t * 0.4f;  if (rock  > 1.f) rock  = 1.f;
        dirt  = 0.20f * (1.f - t);
        break;
    default:
        grass = 1.0f - t * 1.6f;   if (grass < 0.f) grass = 0.f;
        dirt  = (1.0f - t) * 0.3f;
        rock  = t * 0.9f;           if (rock  > 1.f) rock  = 1.f;
        break;
    }

    float sum = grass + rock + dirt + snow;
    if (sum > 1e-6f) { grass /= sum; rock /= sum; dirt /= sum; snow /= sum; }
    splat[0] = grass; splat[1] = rock; splat[2] = dirt; splat[3] = snow;
}

// ── TerrainGen_Build ──────────────────────────────────────────────────────────

static TerrainVertex s_verts_buf[TERRAIN_VERTS];
static uint16_t      s_idx_buf  [TERRAIN_IDX];
// separate float[] nav positions (x,y,z per vert)
static float         s_nav_pos  [TERRAIN_VERTS * 3];
static int           s_nav_tri  [TERRAIN_IDX];   // same indices, cast to int
// Item 7: skirt staging buffers (4 edges × 65 × 2 verts, 4 × 64 × 6 indices)
static TerrainVertex s_skirt_v  [TERRAIN_SKIRT_VERTS];  // 520 verts
static uint16_t      s_skirt_i  [TERRAIN_SKIRT_IDX];    // 1536 indices

// Load r32 heightmap file into a flat buffer (width*height floats).
// Returns false on error. Caller owns buffer (use delete[]).
static bool s_load_r32(const char* path, float*& buf_out, int& w_out, int& h_out,
                        float& hmin_out, float& hmax_out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    unsigned int w, h;
    float hmin, hmax;
    if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 ||
        fread(&hmin, 4, 1, f) != 1 || fread(&hmax, 4, 1, f) != 1) {
        fclose(f); return false;
    }
    buf_out  = new float[w * h];
    w_out    = (int)w; h_out = (int)h;
    hmin_out = hmin;   hmax_out = hmax;
    bool ok  = fread(buf_out, sizeof(float), w * h, f) == w * h;
    fclose(f);
    if (!ok) { delete[] buf_out; buf_out = nullptr; return false; }
    return true;
}

static float s_hmap_sample(const float* hmap, int hmap_w, int hmap_h,
                             float u, float v) {
    // Bilinear sample; u,v in [0,1]
    float fx = u * (hmap_w - 1);
    float fz = v * (hmap_h - 1);
    int   x0 = (int)fx, z0 = (int)fz;
    int   x1 = x0 + 1 < hmap_w ? x0 + 1 : x0;
    int   z1 = z0 + 1 < hmap_h ? z0 + 1 : z0;
    float tx = fx - x0, tz = fz - z0;
    float h00 = hmap[z0 * hmap_w + x0];
    float h10 = hmap[z0 * hmap_w + x1];
    float h01 = hmap[z1 * hmap_w + x0];
    float h11 = hmap[z1 * hmap_w + x1];
    float h0  = h00 + tx * (h10 - h00);
    float h1  = h01 + tx * (h11 - h01);
    return h0 + tz * (h1 - h0);
}

bool TerrainGen_Build(TerrainChunk& out, ChunkCoord coord, const TerrainGenParams& p) {
    float world_origin_x = coord.x * CHUNK_SIZE;
    float world_origin_z = coord.z * CHUNK_SIZE;

    // ── 1. Heights ────────────────────────────────────────────────────────────
    if (!p.force_noise && p.zone_origin_x >= 0) {
        int zx = p.zone_origin_x + coord.x;
        int zy = p.zone_origin_z + coord.z;
        bool loaded = false;
        if (s_atlas_loaded && zx >= 0 && zx < ATLAS_ZONES && zy >= 0 && zy < ATLAS_ZONES) {
            // Atlas path — direct copy from in-RAM buffer (no file I/O per chunk)
            const float* src = &s_atlas_h[s_atlas_zi(zx, zy) * ATLAS_ZBLOCK];
            for (int row = 0; row <= TERRAIN_GRID; ++row)
                for (int col = 0; col <= TERRAIN_GRID; ++col)
                    out.heightmap.h[s_idx(col, row)] = src[row * ATLAS_VERTS + col];
            loaded = true;
        }
        if (!loaded) {
            // Fallback: individual zone_X_Y.r32 files
            char path[256];
            snprintf(path, sizeof(path), "game/data/terrain/zone_%d_%d.r32", zx, zy);
            float* hmap = nullptr; int hw = 0, hh = 0; float hmin, hmax;
            if (s_load_r32(path, hmap, hw, hh, hmin, hmax) && hmap) {
                for (int row = 0; row <= TERRAIN_GRID; ++row)
                    for (int col = 0; col <= TERRAIN_GRID; ++col)
                        out.heightmap.h[s_idx(col, row)] = hmap[row * (TERRAIN_GRID+1) + col];
                delete[] hmap;
                loaded = true;
            }
        }
        if (!loaded) {
            // Item 3: atlas height fallback — copy border row/col from nearest loaded neighbor
            // instead of dropping to h=0 (which creates a visible void/cliff at atlas edge).
            bool neighbor_found = false;
            const int try_dx[] = { 1, -1, 0,  0 };
            const int try_dz[] = { 0,  0, 1, -1 };
            for (int d = 0; d < 4 && !neighbor_found; ++d) {
                int nx = zx + try_dx[d], nz = zy + try_dz[d];
                if (nx < 0 || nx >= ATLAS_ZONES || nz < 0 || nz >= ATLAS_ZONES) continue;
                if (s_atlas_hmax[s_atlas_zi(nx, nz)] - s_atlas_hmin[s_atlas_zi(nx, nz)] < 0.1f) continue;
                // Clone neighbor heights into this zone
                for (int row = 0; row <= TERRAIN_GRID; ++row)
                    for (int col = 0; col <= TERRAIN_GRID; ++col)
                        out.heightmap.h[s_idx(col, row)] =
                            s_atlas_h[s_atlas_hi(nx, nz, col, row)];
                neighbor_found = true;
            }
            if (!neighbor_found) {
                fprintf(stdout, "[TerrainGen] zone %d/%d missing — noise fallback\n", zx, zy);
                TerrainGenParams fp = p;
                fp.zone_origin_x = -1;
                for (int row = 0; row <= TERRAIN_GRID; ++row)
                    for (int col = 0; col <= TERRAIN_GRID; ++col)
                        out.heightmap.h[s_idx(col, row)] = s_gen_height(col, row, coord, fp);
            }
        }
    } else if (!p.force_noise && p.heightmap_r32) {
        // Load Kenshi heightmap and sample heights for this chunk
        static const float* s_hmap     = nullptr;
        static int          s_hmap_w   = 0, s_hmap_h = 0;
        static float        s_hmap_range = 1.0f;
        static const char*  s_hmap_path = nullptr;

        if (s_hmap_path != p.heightmap_r32) {
            delete[] s_hmap;
            float hmin, hmax;
            float* tmp = nullptr;
            if (s_load_r32(p.heightmap_r32, tmp, s_hmap_w, s_hmap_h, hmin, hmax)) {
                s_hmap      = tmp;
                s_hmap_range = (hmax - hmin > 1e-3f) ? (hmax - hmin) : 1.0f;
                s_hmap_path  = p.heightmap_r32;
            } else {
                s_hmap = nullptr; s_hmap_path = nullptr;
            }
        }

        float total_size = 7 * CHUNK_SIZE;  // legacy single-file mode: 7×7 grid
        for (int row = 0; row <= TERRAIN_GRID; ++row) {
            for (int col = 0; col <= TERRAIN_GRID; ++col) {
                float wx  = (coord.x * CHUNK_SIZE + col * TERRAIN_STEP) + p.world_offset_x;
                float wz  = (coord.z * CHUNK_SIZE + row * TERRAIN_STEP) + p.world_offset_z;
                float u   = (wx - p.world_offset_x + total_size * 0.5f) / total_size;
                float v   = (wz - p.world_offset_z + total_size * 0.5f) / total_size;
                u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
                v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
                float raw = s_hmap ? s_hmap_sample(s_hmap, s_hmap_w, s_hmap_h, u, v) : 0.f;
                // Normalize raw to [-1,1] then scale by amplitude
                float h = (raw / s_hmap_range) * p.amplitude;
                out.heightmap.h[s_idx(col, row)] = h;
            }
        }
    } else {
        for (int row = 0; row <= TERRAIN_GRID; ++row) {
            for (int col = 0; col <= TERRAIN_GRID; ++col) {
                out.heightmap.h[s_idx(col, row)] = s_gen_height(col, row, coord, p);
            }
        }
    }
    // (zone_origin_x >= 0 branch closes above via its own else-if)

    // ── 2. Vertices ────────────────────────────────────────────────────────────
    for (int row = 0; row <= TERRAIN_GRID; ++row) {
        for (int col = 0; col <= TERRAIN_GRID; ++col) {
            int vi = s_idx(col, row);
            float h = out.heightmap.h[vi];
            float wx = world_origin_x + col * TERRAIN_STEP + p.world_offset_x;
            float wz = world_origin_z + row * TERRAIN_STEP + p.world_offset_z;

            s_verts_buf[vi].x = wx;
            s_verts_buf[vi].y = h;
            s_verts_buf[vi].z = wz;

            // UV: repeat every ~8m so texture looks natural at any terrain scale
            s_verts_buf[vi].u = wx * 0.125f;
            s_verts_buf[vi].v = wz * 0.125f;

            // Biome-aware splat weights with soft boundary blending.
            // Sample temperature/moisture at 2× the biome_scale for a second
            // "offset" position, then lerp between the two resolved biomes by a
            // smooth-step on the noise magnitude — this blurs hard biome edges
            // over ~200-400m without extra noise calls.
            if (p.biome_scale > 0.f) {
                float bs = p.biome_scale;
                float so = (float)p.seed;

                float raw_t = SimplexNoise2(wx*bs + so*0.7f + 1000.f*bs,
                                            wz*bs + so*0.3f + 1000.f*bs);
                float raw_m = SimplexNoise2(wx*bs + so*0.4f + 2000.f*bs,
                                            wz*bs + so*0.9f + 2000.f*bs);
                float temp0 = (raw_t + 1.f) * 0.5f;
                float mois0 = (raw_m + 1.f) * 0.5f;
                BiomeType b0 = BiomeResolver::Resolve(temp0, mois0, h);

                // Offset sample for blending (half frequency → smoother transitions)
                float raw_t2 = SimplexNoise2(wx*bs*0.5f + so*0.7f + 1000.f*bs + 37.3f,
                                             wz*bs*0.5f + so*0.3f + 1000.f*bs + 19.7f);
                float raw_m2 = SimplexNoise2(wx*bs*0.5f + so*0.4f + 2000.f*bs + 53.1f,
                                             wz*bs*0.5f + so*0.9f + 2000.f*bs + 41.9f);
                float temp1 = (raw_t2 + 1.f) * 0.5f;
                float mois1 = (raw_m2 + 1.f) * 0.5f;
                BiomeType b1 = BiomeResolver::Resolve(temp1, mois1, h);

                if (b0 == b1) {
                    s_splat(h, p.amplitude, s_verts_buf[vi].splat, b0);
                } else {
                    // Blend: smooth-step on the difference of noise magnitudes
                    float diff = fabsf(raw_t - raw_t2) + fabsf(raw_m - raw_m2);
                    float t_blend = diff * 2.5f;  // scale → [0,1] range
                    if (t_blend > 1.f) t_blend = 1.f;
                    t_blend = t_blend * t_blend * (3.f - 2.f * t_blend); // smooth-step
                    float s0[4], s1[4];
                    s_splat(h, p.amplitude, s0, b0);
                    s_splat(h, p.amplitude, s1, b1);
                    for (int k = 0; k < 4; ++k)
                        s_verts_buf[vi].splat[k] = s0[k] + t_blend * (s1[k] - s0[k]);
                }
            } else {
                s_splat(h, p.amplitude, s_verts_buf[vi].splat, BiomeType::Desert);
            }

            // Variant B: edge-fade — fade splat to 0 at chunk boundary (outer 15%).
            // Both sides of any seam → pure overlay → no visible biome stitch.
            {
                float lx = (float)col / TERRAIN_GRID;
                float lz = (float)row / TERRAIN_GRID;
                float et = lx < (1.f-lx) ? lx : (1.f-lx);
                if (lz      < et) et = lz;
                if ((1.f-lz)< et) et = 1.f-lz;
                et /= 0.15f;
                if (et < 1.f) {
                    float f = et * et * (3.f - 2.f * et);
                    for (int k = 0; k < 4; ++k) s_verts_buf[vi].splat[k] *= f;
                }
            }

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
            // Reversed winding for normal accumulation so normals point +Y (upward).
            // Vertex winding in world space (XZ plane, Y up) is CW for the render
            // pipeline (SDL_GPU Vulkan uses Y-down NDC after viewport flip → no cull).
            accum_normal(bl, tl, br);
            accum_normal(tl, tr, br);

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

    // ── Variant C: slope-based splat (overwrites Variant B noise splat) ─────────
    // splat[0]=flat  splat[1]=slope  splat[2]=cliff  splat[3]=0
    // Matches BiomeDef tex0-3 semantic: flat/slope/cliff per biome.
    for (int i = 0; i < TERRAIN_VERTS; ++i) {
        float ny    = s_verts_buf[i].ny;          // 1=flat, 0=vertical
        float flat_w  = ny * ny;
        float slope_w = 4.f * ny * (1.f - ny);
        float cliff_w = (1.f - ny) * (1.f - ny);
        float sum = flat_w + slope_w + cliff_w;
        if (sum > 1e-6f) { flat_w /= sum; slope_w /= sum; cliff_w /= sum; }

        int col_ = i % (TERRAIN_GRID + 1);
        int row_ = i / (TERRAIN_GRID + 1);
        float lx = (float)col_ / TERRAIN_GRID;
        float lz = (float)row_ / TERRAIN_GRID;
        float et = lx < (1.f-lx) ? lx : (1.f-lx);
        if (lz       < et) et = lz;
        if ((1.f-lz) < et) et = 1.f-lz;
        et /= 0.15f;
        float f = et < 1.f ? et*et*(3.f-2.f*et) : 1.f;

        s_verts_buf[i].splat[0] = flat_w  * f;
        s_verts_buf[i].splat[1] = slope_w * f;
        s_verts_buf[i].splat[2] = cliff_w * f;
        s_verts_buf[i].splat[3] = 0.f;
    }

    // ── Cross-chunk normal stitching ──────────────────────────────────────────
    // Edge vertices only have normals from triangles within this chunk.
    // Two paths: atlas (zone_origin_x>=0) samples the BSS atlas; noise path
    // calls s_gen_height on the adjacent chunk coord.

    // Noise-path stitching: sample neighbor heights via s_gen_height.
    // s_edge(col,row): col/row may be -1 or TERRAIN_GRID+1 — wraps to neighbor chunk.
    if (p.force_noise) {
        auto s_edge = [&](int col, int row) -> float {
            ChunkCoord nc = coord;
            if      (col < 0)              { nc.x--; col += TERRAIN_GRID; }
            else if (col > TERRAIN_GRID)   { nc.x++; col -= TERRAIN_GRID; }
            if      (row < 0)              { nc.z--; row += TERRAIN_GRID; }
            else if (row > TERRAIN_GRID)   { nc.z++; row -= TERRAIN_GRID; }
            return s_gen_height(col, row, nc, p);
        };

        auto fix_n = [&](int col, int row,
                         float hL, float hR, float hD, float hU) {
            float dhdx = (hR - hL) / (2.0f * TERRAIN_STEP);
            float dhdz = (hU - hD) / (2.0f * TERRAIN_STEP);
            float nx = -dhdx, ny = 1.0f, nz = -dhdz;
            float len = sqrtf(nx*nx + ny*ny + nz*nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            int vi = s_idx(col, row);
            s_verts_buf[vi].nx = nx;
            s_verts_buf[vi].ny = ny;
            s_verts_buf[vi].nz = nz;
        };

        for (int row = 1; row < TERRAIN_GRID; ++row) {
            fix_n(TERRAIN_GRID, row,
                out.heightmap.h[s_idx(TERRAIN_GRID-1, row)],
                s_edge(TERRAIN_GRID+1, row),
                out.heightmap.h[s_idx(TERRAIN_GRID, row-1)],
                out.heightmap.h[s_idx(TERRAIN_GRID, row+1)]);
            fix_n(0, row,
                s_edge(-1, row),
                out.heightmap.h[s_idx(1, row)],
                out.heightmap.h[s_idx(0, row-1)],
                out.heightmap.h[s_idx(0, row+1)]);
        }
        for (int col = 1; col < TERRAIN_GRID; ++col) {
            fix_n(col, 0,
                out.heightmap.h[s_idx(col-1, 0)],
                out.heightmap.h[s_idx(col+1, 0)],
                s_edge(col, -1),
                out.heightmap.h[s_idx(col, 1)]);
            fix_n(col, TERRAIN_GRID,
                out.heightmap.h[s_idx(col-1, TERRAIN_GRID)],
                out.heightmap.h[s_idx(col+1, TERRAIN_GRID)],
                out.heightmap.h[s_idx(col, TERRAIN_GRID-1)],
                s_edge(col, TERRAIN_GRID+1));
        }
        // Four corners: all four neighbours contribute — sample diagonals via s_edge.
        const int G = TERRAIN_GRID;
        fix_n(0,  0,  s_edge(-1,0),   out.heightmap.h[s_idx(1,0)],  s_edge(0,-1),  out.heightmap.h[s_idx(0,1)]);
        fix_n(G,  0,  out.heightmap.h[s_idx(G-1,0)], s_edge(G+1,0), s_edge(G,-1),  out.heightmap.h[s_idx(G,1)]);
        fix_n(0,  G,  s_edge(-1,G),   out.heightmap.h[s_idx(1,G)],  out.heightmap.h[s_idx(0,G-1)], s_edge(0,G+1));
        fix_n(G,  G,  out.heightmap.h[s_idx(G-1,G)], s_edge(G+1,G), out.heightmap.h[s_idx(G,G-1)], s_edge(G,G+1));
    }

    // Atlas-path stitching (unchanged).
    if (!p.force_noise && p.zone_origin_x >= 0 && s_atlas_loaded) {
        int zx0 = p.zone_origin_x + coord.x;
        int zy0 = p.zone_origin_z + coord.z;

        // Get height from atlas with bounds check
        auto atlas_h = [&](int zx, int zy, int col, int row) -> float {
            if (zx < 0 || zx >= ATLAS_ZONES || zy < 0 || zy >= ATLAS_ZONES) return 0.f;
            col = col < 0 ? 0 : (col > ATLAS_VERTS-1 ? ATLAS_VERTS-1 : col);
            row = row < 0 ? 0 : (row > ATLAS_VERTS-1 ? ATLAS_VERTS-1 : row);
            return s_atlas_h[s_atlas_hi(zx, zy, col, row)];
        };

        auto fix_normal = [&](int col, int row,
                               float hL, float hR, float hD, float hU) {
            float dhdx = (hR - hL) / (2.0f * TERRAIN_STEP);
            float dhdz = (hU - hD) / (2.0f * TERRAIN_STEP);
            float nx = -dhdx, ny = 1.0f, nz = -dhdz;
            float len = sqrtf(nx*nx + ny*ny + nz*nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            int vi = s_idx(col, row);
            s_verts_buf[vi].nx = nx;
            s_verts_buf[vi].ny = ny;
            s_verts_buf[vi].nz = nz;
        };

        // Right edge (col = TERRAIN_GRID): hR comes from col=1 of zone (zx0+1)
        for (int row = 1; row < TERRAIN_GRID; ++row) {
            fix_normal(TERRAIN_GRID, row,
                out.heightmap.h[s_idx(TERRAIN_GRID-1, row)],
                atlas_h(zx0+1, zy0, 1, row),
                out.heightmap.h[s_idx(TERRAIN_GRID, row-1)],
                out.heightmap.h[s_idx(TERRAIN_GRID, row+1)]);
        }
        // Left edge (col = 0): hL comes from col=TERRAIN_GRID-1 of zone (zx0-1)
        for (int row = 1; row < TERRAIN_GRID; ++row) {
            fix_normal(0, row,
                atlas_h(zx0-1, zy0, TERRAIN_GRID-1, row),
                out.heightmap.h[s_idx(1, row)],
                out.heightmap.h[s_idx(0, row-1)],
                out.heightmap.h[s_idx(0, row+1)]);
        }
        // Bottom edge (row = 0): hD comes from row=TERRAIN_GRID-1 of zone (zx0, zy0-1)
        for (int col = 1; col < TERRAIN_GRID; ++col) {
            fix_normal(col, 0,
                out.heightmap.h[s_idx(col-1, 0)],
                out.heightmap.h[s_idx(col+1, 0)],
                atlas_h(zx0, zy0-1, col, TERRAIN_GRID-1),
                out.heightmap.h[s_idx(col, 1)]);
        }
        // Top edge (row = TERRAIN_GRID): hU comes from row=1 of zone (zx0, zy0+1)
        for (int col = 1; col < TERRAIN_GRID; ++col) {
            fix_normal(col, TERRAIN_GRID,
                out.heightmap.h[s_idx(col-1, TERRAIN_GRID)],
                out.heightmap.h[s_idx(col+1, TERRAIN_GRID)],
                out.heightmap.h[s_idx(col, TERRAIN_GRID-1)],
                atlas_h(zx0, zy0+1, col, 1));
        }
    }

    // ── Geomorph targets (L0→L1 transition) ──────────────────────────────────────
    // Guarded: LOD IBO disabled in RD-4. Re-enable by setting TERRAIN_GEOMORPH_ENABLED 1
    // in terrain_pom.vert. morph_y field kept in TerrainVertex for easy re-enable.
#if TERRAIN_GEOMORPH_ENABLED
    for (int row = 0; row <= TERRAIN_GRID; ++row) {
        for (int col = 0; col <= TERRAIN_GRID; ++col) {
            int vi = s_idx(col, row);
            float fy;
            if ((col & 1) == 0 && (row & 1) == 0) {
                fy = s_verts_buf[vi].y;
            } else if ((col & 1) != 0 && (row & 1) == 0) {
                fy = (s_verts_buf[s_idx(col-1, row)].y +
                      s_verts_buf[s_idx(col+1, row)].y) * 0.5f;
            } else if ((col & 1) == 0 && (row & 1) != 0) {
                fy = (s_verts_buf[s_idx(col, row-1)].y +
                      s_verts_buf[s_idx(col, row+1)].y) * 0.5f;
            } else {
                fy = (s_verts_buf[s_idx(col-1, row-1)].y +
                      s_verts_buf[s_idx(col+1, row-1)].y +
                      s_verts_buf[s_idx(col-1, row+1)].y +
                      s_verts_buf[s_idx(col+1, row+1)].y) * 0.25f;
            }
            s_verts_buf[vi].morph_y = fy;
        }
    }
#endif

    // ── Item 7: Geometry skirt ────────────────────────────────────────────────────
    // Hang 2m-deep quads from each edge to close gaps between adjacent chunks.
    // 4 edges × (TERRAIN_GRID+1) × 2 verts; indices are skirt-local (0-based).
    {
        const float SKIRT_DROP = 2.0f;
        int sv = 0, si = 0;

        auto add_strip = [&](int col0, int row0, int dc, int dr) {
            uint16_t base = (uint16_t)sv;
            for (int k = 0; k <= TERRAIN_GRID; ++k) {
                int col = col0 + dc * k, row = row0 + dr * k;
                int vi = s_idx(col, row);
                s_skirt_v[sv]   = s_verts_buf[vi];                   // top
                s_skirt_v[sv+1] = s_verts_buf[vi];                   // bottom
                s_skirt_v[sv+1].y      -= SKIRT_DROP;
                s_skirt_v[sv+1].morph_y -= SKIRT_DROP;
                sv += 2;
                if (k < TERRAIN_GRID) {
                    uint16_t t = base + (uint16_t)(k * 2);
                    s_skirt_i[si++] = t;     s_skirt_i[si++] = t+1; s_skirt_i[si++] = t+2;
                    s_skirt_i[si++] = t+2;   s_skirt_i[si++] = t+1; s_skirt_i[si++] = t+3;
                }
            }
        };

        add_strip(0,            0,            1, 0);  // South edge (row=0)
        add_strip(0,            TERRAIN_GRID, 1, 0);  // North edge (row=GRID)
        add_strip(0,            0,            0, 1);  // West  edge (col=0)
        add_strip(TERRAIN_GRID, 0,            0, 1);  // East  edge (col=GRID)
    }

    out.coord    = coord;
    out.center_x = p.world_offset_x + coord.x * CHUNK_SIZE + CHUNK_SIZE * 0.5f;
    out.center_z = p.world_offset_z + coord.z * CHUNK_SIZE + CHUNK_SIZE * 0.5f;
    out.loaded   = false;  // Upload() sets this

    // Biome ground-texture lookup — zone slug → GroundTexLayer indices per splat channel
    if (p.zone_origin_x >= 0) {
        int zx = p.zone_origin_x + coord.x;
        int zy = p.zone_origin_z + coord.z;
        int nz = 0;
        const ZoneRecord* zones = WorldRegistry::Get().GetAll(nz);
        const char* slug = nullptr;
        for (int i = 0; i < nz; ++i)
            if (zones[i].grid_x == zx && zones[i].grid_z == zy) { slug = zones[i].name; break; }
        const BiomeDef& bd = BiomeDef::ForZone(slug);
        out.ground_layers[0] = (float)bd.tex0;
        out.ground_layers[1] = (float)bd.tex1;
        out.ground_layers[2] = (float)bd.tex2;
        out.ground_layers[3] = (float)bd.tex3;
    }

    // ── 4. NavMesh (disabled) + PassGrid (lightweight, from heightmap slope) ────
    // Per-chunk NavMesh disabled: NPC pathfinding uses NavSystem singleton.
    // Building 256×256 per-chunk navmeshes costs 15+ seconds at startup.
    (void)p.nav_cs; (void)p.nav_ch;

    // L2-inspired TerrainPassGrid: mark cells walkable based on slope.
    // Faster than NavMesh — uses existing heightmap already in out.heightmap.
    // Slope threshold: L2 geodata blocks cells where angle > 45°.
    out.pass_grid.Clear();
    {
        const float max_slope_h = PASS_CELL_SIZE * 1.0f;  // 100% grade ≈ 45°
        float ox = out.coord.x * CHUNK_SIZE;
        float oz = out.coord.z * CHUNK_SIZE;
        (void)ox; (void)oz;
        for (int row = 0; row < PASS_GRID_N; ++row) {
            for (int col = 0; col < PASS_GRID_N; ++col) {
                float lx = (col + 0.5f) * PASS_CELL_SIZE;
                float lz = (row + 0.5f) * PASS_CELL_SIZE;
                float h_c = out.SampleHeight(lx, lz);
                float h_e = out.SampleHeight(lx + PASS_CELL_SIZE, lz);
                float h_n = out.SampleHeight(lx, lz + PASS_CELL_SIZE);
                float dh  = (h_e - h_c) > 0.f ? (h_e - h_c) : -(h_e - h_c);
                float dhz = (h_n - h_c) > 0.f ? (h_n - h_c) : -(h_n - h_c);
                if (dh < max_slope_h && dhz < max_slope_h)
                    out.pass_grid.SetWalkable(row, col);
            }
        }
    }
    return true;
}

// Accessors for the staging buffers — used by terrain_upload.cpp (GPU side).
// Kept here so GPU code does not share this translation unit (avoids pulling
// glad symbols into test binaries that only call TerrainGen_Build).
const TerrainVertex* TerrainGen_StagedVerts()         { return s_verts_buf; }
const uint16_t*      TerrainGen_StagedIndices()       { return s_idx_buf;   }
const TerrainVertex* TerrainGen_StagedSkirtVerts()    { return s_skirt_v;   }
const uint16_t*      TerrainGen_StagedSkirtIndices()  { return s_skirt_i;   }

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
