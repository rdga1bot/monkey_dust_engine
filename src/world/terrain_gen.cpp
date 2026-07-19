#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/world/chunk_def.h>
#include <monkey_dust/world/biome_system.h>
#include <monkey_dust/world/biome_def.h>
#include <monkey_dust/world/terrain_pass_grid.h>
#include <monkey_dust/platform/md_fs.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "stb_image.h"

// ── Biomemap colour lookup (real Kenshi biome-selection mechanism) ───────────
// game/data/textures/md_biomemap.png (1024x1024 RGB, converted 1:1 from
// Kenshi's real tmp_/kenshi/data/newland/land/biomemap.png). CONFIRMED this
// session (tools/md_gen_biome_table.py, not guessed): every real Kenshi
// "Biomes" FCS entry (gamedata.base itemType=28) has an 'index' field — a
// packed Windows COLORREF — that matches an exact pixel colour in this file
// for 10+ directly-verified named regions (desert, Ashlands, Canyonlands,
// Canyonlands Crater, ...). This replaces the old areasmap.tga-based
// nearest-NAMED-zone approximation (kept ~59 named zones only, guessed
// biome parameters by hand) with the real mechanism: sample this exact
// chunk's biomemap colour directly, nearest-match against the real per-biome
// FCS data via BiomeDef::ForColor — precise per-chunk, not per-named-zone.
static uint8_t* s_biomemap     = nullptr;
static int      s_biomemap_w   = 0, s_biomemap_h = 0;
static bool     s_biomemap_tried = false;

static void s_load_biomemap() {
    if (s_biomemap_tried) return;
    s_biomemap_tried = true;
    int comp = 0;
    s_biomemap = stbi_load("game/data/textures/md_biomemap.png", &s_biomemap_w, &s_biomemap_h, &comp, 3);
    if (!s_biomemap)
        fprintf(stderr, "[TerrainGen] md_biomemap.png not found — biome lookup falls back to default\n");
}

// ── Blendmap touch check (task #182e, 2026-07-19: FPS fix) ───────────────────
// game/data/textures/md_biome_blend.png (real Kenshi blendmap.png, 1:1 copy,
// private/md_gen_biome_blendmap.py) — same file terrain_forward.slang samples
// per-pixel for the comboAlternates crossfade. Loaded here ONLY to answer a
// per-CHUNK yes/no question at generation time: does this chunk's zone block
// contain ANY non-zero R/G/B pixel at all? The vast majority of chunks never
// touch any of the 3 rare combo-alternate biomes, so gating the whole 18-
// sample crossfade block behind a per-chunk UNIFORM flag (not a per-pixel
// dynamic branch, which measured ~3x FPS regression on Intel HD 520 — likely
// both sides of short branches with texture fetches get executed and masked
// on this hardware/driver) should recover most of the lost performance:
// SIMD/warp divergence only matters for per-PIXEL branches: a per-chunk
// uniform flag means the ENTIRE draw call's fragments take the same path.
static uint8_t* s_blendmap     = nullptr;
static int      s_blendmap_w   = 0, s_blendmap_h = 0;
static bool     s_blendmap_tried = false;

static void s_load_blendmap() {
    if (s_blendmap_tried) return;
    s_blendmap_tried = true;
    int comp = 0;
    s_blendmap = stbi_load("game/data/textures/md_biome_blend.png", &s_blendmap_w, &s_blendmap_h, &comp, 4);
    if (!s_blendmap)
        fprintf(stderr, "[TerrainGen] md_biome_blend.png not found — combo-alternate gating defaults to always-on (safe, slower)\n");
}

// zx,zy: 0..63 zone grid coords (chunk==zone, 1:1, CHUNK_SIZE==zone size).
// Returns true if ANY pixel in this zone's blendmap block (dilated by 1
// pixel on each side, to catch GPU bilinear-filter bleed from an adjacent
// zone's non-zero pixels right at the shared border) has R, G, or B > 0.
// Conservative by design (checks whole own block + margin, not exact
// footprint) — a false positive just costs one skipped-uniform-branch's
// worth of unnecessary sampling on a chunk that turns out fully zero-weight
// anyway (cheap); a false negative would silently drop a real, visible
// crossfade (expensive to debug) — this trades a little perf for zero risk
// of reintroducing the "seams" bug via an over-eager skip.
static bool s_blendmap_touch(int zx, int zy) {
    if (!s_blendmap) return true; // no data loaded — never silently skip
    float scale = (float)s_blendmap_w / 64.0f;
    int x0 = (int)(zx * scale) - 1, x1 = (int)((zx + 1) * scale) + 1;
    int y0 = (int)(zy * scale) - 1, y1 = (int)((zy + 1) * scale) + 1;
    if (x0 < 0) x0 = 0; if (x1 > s_blendmap_w) x1 = s_blendmap_w;
    if (y0 < 0) y0 = 0; if (y1 > s_blendmap_h) y1 = s_blendmap_h;
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const uint8_t* p = s_blendmap + ((size_t)py * s_blendmap_w + px) * 4;
            if (p[0] != 0 || p[1] != 0 || p[2] != 0) return true;
        }
    }
    return false;
}

// zx,zy: 0..63 zone grid coords. Returns false (no data) if biomemap missing.
// Mode (most frequent colour) over the zone's whole pixel block, not a
// single centre-pixel sample — same noise-robustness rationale as the old
// areasmap lookup this replaces (stray anti-aliased pixels at painted
// region edges would otherwise flip an occasional chunk to the wrong biome).
static bool s_biomemap_color(int zx, int zy, uint8_t out[3]) {
    if (!s_biomemap) return false;
    float scale = (float)s_biomemap_w / 64.0f;
    int x0 = (int)(zx * scale), x1 = (int)((zx + 1) * scale);
    int y0 = (int)(zy * scale), y1 = (int)((zy + 1) * scale);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x0 < 0) x0 = 0; if (x1 > s_biomemap_w) x1 = s_biomemap_w;
    if (y0 < 0) y0 = 0; if (y1 > s_biomemap_h) y1 = s_biomemap_h;
    if (x0 >= x1 || y0 >= y1) return false;

    static constexpr int MAX_DISTINCT = 32;
    uint8_t colors[MAX_DISTINCT][3];
    int counts[MAX_DISTINCT] = {};
    int n_distinct = 0;

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const uint8_t* p = s_biomemap + ((size_t)py * s_biomemap_w + px) * 3;
            int found = -1;
            for (int i = 0; i < n_distinct; ++i) {
                if (colors[i][0] == p[0] && colors[i][1] == p[1] && colors[i][2] == p[2]) { found = i; break; }
            }
            if (found < 0 && n_distinct < MAX_DISTINCT) {
                found = n_distinct++;
                colors[found][0] = p[0]; colors[found][1] = p[1]; colors[found][2] = p[2];
            }
            if (found >= 0) counts[found]++;
        }
    }
    if (n_distinct == 0) return false;

    int best = 0;
    for (int i = 1; i < n_distinct; ++i) if (counts[i] > counts[best]) best = i;
    out[0] = colors[best][0]; out[1] = colors[best][1]; out[2] = colors[best][2];
    return true;
}

// Factored out so it can also be called for a chunk's 4 grid-neighbours
// (biome crossfade target, see blend_layers in terrain_chunk.h).
static const BiomeDef& s_resolve_biome(int zx, int zy) {
    uint8_t col[3];
    const BiomeRegistry& biomes = BiomeRegistry::Get();
    if (s_biomemap_color(zx, zy, col))
        return biomes.ForColor(col[0], col[1], col[2]);
    return biomes.ForZone(nullptr);
}

const BiomeDef& TerrainGen_ResolveBiome(int zx, int zy) {
    return s_resolve_biome(zx, zy);
}

// ── Terrain Atlas ─────────────────────────────────────────────────────────────
// 2026-07-19: on-disk format switched from a single 260MB float32 .r32 blob
// to a raw-uint16 "base" (read-only, written offline by tools/tif_to_r32.py
// — matches Kenshi's own uint16 source precision exactly) plus a small
// sparse raw "_edits.r32" overlay file the editor's terrain brush writes to.
// A 16-bit PNG base was tried first (2.7x smaller, 50.6MB vs 136.3MB) but
// measured ~5x SLOWER to load in this exact engine (1.79s vs 0.35s,
// stb_image DEFLATE decode cost) — this project's target hardware (Intel
// HD 520) has a documented history of user-reported slow startup
// (CLAUDE.md task #158c), so load time won over disk size; raw uint16 is
// still under half the size of the original float32 .r32. The base is
// still never edited in-place (even though raw uint16 COULD support
// in-place zone writes, a sparse edits-overlay is simpler and keeps every
// brush stroke a small, fast append instead of a full-file rewrite). See
// TERRAIN_HEIGHT_SCALE_M's doc comment for the uint16<->metres mapping
// (identical to tools/tif_to_r32.py's / tools/md_hmap_io.py's).
static constexpr uint32_t ATLAS_R16_MAGIC   = 0x3631524Du; // "MR16" LE — matches tools/md_hmap_io.py's R16_MAGIC
static constexpr uint32_t ATLAS_EDITS_MAGIC = 0x4541444Du; // "MDAE" LE
static constexpr int      ATLAS_ZONES  = 64;
static constexpr int      ATLAS_VERTS  = 129;          // TERRAIN_GRID+1
static constexpr int      ATLAS_ZBLOCK = ATLAS_VERTS * ATLAS_VERTS; // 16641
static constexpr int      ATLAS_R16_SIZE = ATLAS_ZONES * ATLAS_VERTS; // 8256, r16 grid side
// Must match tools/tif_to_r32.py's/tools/md_hmap_io.py's HEIGHT_MAX_M
// exactly — this is the global uint16[0..65535] <-> metres[0..980] scale
// the raw-uint16 base is quantized with. Kenshi's own fullmap.tif source
// is ALREADY uint16 at this same effective precision (~0.015m/step), so
// this round-trip is lossless relative to the real source data, not a new
// quantization step.
static constexpr float    TERRAIN_HEIGHT_SCALE_M = 980.0f;

static float   s_atlas_h   [ATLAS_ZONES * ATLAS_ZONES * ATLAS_ZBLOCK]; // ~260 MB BSS
static float   s_atlas_hmin[ATLAS_ZONES * ATLAS_ZONES];
static float   s_atlas_hmax[ATLAS_ZONES * ATLAS_ZONES];
static uint8_t s_atlas_dirty[ATLAS_ZONES * ATLAS_ZONES]; // "ever edited this session" — NOT reset on save (see TerrainAtlas_Save)
static bool    s_atlas_loaded = false;
static char    s_atlas_base_path[512] = {}; // base path with no extension, as passed by the caller

static int s_atlas_zi(int zx, int zy)          { return zy * ATLAS_ZONES + zx; }
static int s_atlas_hi(int zx, int zy, int c, int r) {
    return s_atlas_zi(zx, zy) * ATLAS_ZBLOCK + r * ATLAS_VERTS + c;
}

// Derives "<base>.r16" / "<base>_edits.r32" from the caller's base path.
// Callers pass a path with no extension (e.g. "game/data/terrain/world_hmap");
// tolerate an accidental trailing ".r32" from old call sites by stripping it.
static void s_atlas_derive_paths(const char* base, char* r16_out, char* edits_out, size_t out_sz) {
    char stripped[512];
    strncpy(stripped, base, sizeof(stripped) - 1);
    stripped[sizeof(stripped) - 1] = '\0';
    size_t len = strlen(stripped);
    if (len > 4 && strcmp(stripped + len - 4, ".r32") == 0) stripped[len - 4] = '\0';
    snprintf(r16_out,   out_sz, "%s.r16",       stripped);
    snprintf(edits_out, out_sz, "%s_edits.r32", stripped);
}

// Loads the read-only raw-uint16 base into s_atlas_h (tiled 8256x8256 =
// 64x64 zones of 129x129, exactly mirroring the old .r32's per-zone layout
// — see tools/tif_to_r32.py). hmin/hmax per zone are recomputed by
// scanning (the file carries no per-zone range, only the single global
// TERRAIN_HEIGHT_SCALE_M). 8-byte header (magic, zone count) — see
// tools/md_hmap_io.py's save_atlas_tiled for the writer.
static bool s_atlas_load_r16_base(const char* r16_path) {
    FILE* f = fopen(r16_path, "rb");
    if (!f) return false;
    uint32_t magic = 0, zones = 0;
    if (fread(&magic, 4, 1, f) != 1 || fread(&zones, 4, 1, f) != 1 ||
        magic != ATLAS_R16_MAGIC || zones != (uint32_t)ATLAS_ZONES) {
        fprintf(stderr, "[TerrainAtlas] %s: bad r16 header (magic=%#x zones=%u)\n", r16_path, magic, zones);
        fclose(f);
        return false;
    }
    std::vector<uint16_t> raw((size_t)ATLAS_R16_SIZE * ATLAS_R16_SIZE);
    bool ok = fread(raw.data(), 2, raw.size(), f) == raw.size();
    fclose(f);
    if (!ok) {
        fprintf(stderr, "[TerrainAtlas] %s: truncated (expected %zu uint16 samples)\n", r16_path, raw.size());
        return false;
    }
    for (int zy = 0; zy < ATLAS_ZONES; ++zy) {
        for (int zx = 0; zx < ATLAS_ZONES; ++zx) {
            int zi = s_atlas_zi(zx, zy);
            float hmin =  1e30f, hmax = -1e30f;
            for (int row = 0; row < ATLAS_VERTS; ++row) {
                const uint16_t* src_row = raw.data() + (size_t)(zy * ATLAS_VERTS + row) * ATLAS_R16_SIZE + zx * ATLAS_VERTS;
                float* dst_row = &s_atlas_h[s_atlas_hi(zx, zy, 0, row)];
                for (int col = 0; col < ATLAS_VERTS; ++col) {
                    float hgt = src_row[col] * (TERRAIN_HEIGHT_SCALE_M / 65535.0f);
                    dst_row[col] = hgt;
                    if (hgt < hmin) hmin = hgt;
                    if (hgt > hmax) hmax = hgt;
                }
            }
            s_atlas_hmin[zi] = hmin;
            s_atlas_hmax[zi] = hmax;
        }
    }
    return true;
}

// Overlays any previously-saved zone edits (sparse list) on top of the
// already-loaded PNG base. Missing file is expected/silent (no edits yet).
static void s_atlas_load_edits(const char* edits_path) {
    FILE* f = fopen(edits_path, "rb");
    if (!f) return;
    uint32_t magic = 0, n_zones = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != ATLAS_EDITS_MAGIC ||
        fread(&n_zones, 4, 1, f) != 1) {
        fclose(f); return;
    }
    uint32_t applied = 0;
    for (uint32_t i = 0; i < n_zones; ++i) {
        uint32_t zx = 0, zy = 0;
        float hmin = 0.f, hmax = 0.f;
        if (fread(&zx, 4, 1, f) != 1 || fread(&zy, 4, 1, f) != 1 ||
            fread(&hmin, 4, 1, f) != 1 || fread(&hmax, 4, 1, f) != 1 ||
            zx >= (uint32_t)ATLAS_ZONES || zy >= (uint32_t)ATLAS_ZONES) break;
        int zi = s_atlas_zi((int)zx, (int)zy);
        if (fread(&s_atlas_h[zi * ATLAS_ZBLOCK], 4, ATLAS_ZBLOCK, f) != (size_t)ATLAS_ZBLOCK) break;
        s_atlas_hmin[zi] = hmin;
        s_atlas_hmax[zi] = hmax;
        s_atlas_dirty[zi] = 1; // re-mark so a subsequent Save keeps carrying it forward
        ++applied;
    }
    fclose(f);
    fprintf(stdout, "[TerrainAtlas] applied %u edited zone(s) from %s\n", applied, edits_path);
}

bool TerrainAtlas_Load(const char* path) {
    char r16_path[512], edits_path[512];
    s_atlas_derive_paths(path, r16_path, edits_path, sizeof(r16_path));
    if (!s_atlas_load_r16_base(r16_path)) return false;
    memset(s_atlas_dirty, 0, sizeof(s_atlas_dirty));
    s_atlas_load_edits(edits_path);
    s_atlas_loaded = true;
    strncpy(s_atlas_base_path, path, sizeof(s_atlas_base_path) - 1);
    fprintf(stdout, "[TerrainAtlas] loaded %s\n", r16_path);
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
    char r16_path[512], edits_path[512];
    s_atlas_derive_paths(path, r16_path, edits_path, sizeof(r16_path));
    (void)r16_path; // base is read-only, never rewritten here
    FILE* f = fopen(edits_path, "wb");
    if (!f) return false;
    uint32_t n_dirty = 0;
    for (int i = 0; i < ATLAS_ZONES * ATLAS_ZONES; ++i) if (s_atlas_dirty[i]) ++n_dirty;
    uint32_t magic = ATLAS_EDITS_MAGIC;
    fwrite(&magic, 4, 1, f);
    fwrite(&n_dirty, 4, 1, f);
    for (int zy = 0; zy < ATLAS_ZONES; ++zy) {
        for (int zx = 0; zx < ATLAS_ZONES; ++zx) {
            int zi = s_atlas_zi(zx, zy);
            if (!s_atlas_dirty[zi]) continue;
            uint32_t uzx = (uint32_t)zx, uzy = (uint32_t)zy;
            fwrite(&uzx, 4, 1, f);
            fwrite(&uzy, 4, 1, f);
            fwrite(&s_atlas_hmin[zi], 4, 1, f);
            fwrite(&s_atlas_hmax[zi], 4, 1, f);
            fwrite(&s_atlas_h[zi * ATLAS_ZBLOCK], 4, ATLAS_ZBLOCK, f);
        }
    }
    fclose(f);
    fprintf(stdout, "[TerrainAtlas] saved %u edited zone(s) to %s\n", n_dirty, edits_path);
    return true;
}

// SaveEdits/LoadEdits/HasEdits — thin wrappers over the atlas API
// (editor brush tool calls these)
bool TerrainAtlas_SaveEdits(const char* path) { return TerrainAtlas_Save(path); }
bool TerrainAtlas_LoadEdits(const char*)       { return s_atlas_loaded; } // already loaded by TerrainAtlas_Load
bool TerrainAtlas_HasEdits() {
    for (int i = 0; i < ATLAS_ZONES * ATLAS_ZONES; ++i)
        if (s_atlas_dirty[i]) return true;
    return false;
}

// World-space bilinear height sample across the whole 64×64 atlas (wx,wz in
// metres, [0, ATLAS_ZONES*CHUNK_SIZE)). Replaces TerrainMaster_SampleWorld
// for ground-snapping call sites now that the macro-geography layer is gone —
// this samples the REAL Kenshi zone data directly, at full atlas resolution.
float TerrainAtlas_SampleWorld(float wx, float wz) {
    if (!s_atlas_loaded) return 0.f;
    constexpr int GRID_N = ATLAS_ZONES * (ATLAS_VERTS - 1); // 8192 spans, 8193 verts/axis
    const float world_ext = (float)ATLAS_ZONES * CHUNK_SIZE;
    float u = wx / world_ext, v = wz / world_ext;
    u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
    v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);

    float gx = u * GRID_N, gz = v * GRID_N;
    int gx0 = (int)gx; if (gx0 >= GRID_N) gx0 = GRID_N - 1;
    int gz0 = (int)gz; if (gz0 >= GRID_N) gz0 = GRID_N - 1;
    float tx = gx - gx0, tz = gz - gz0;

    auto sample_grid = [](int gcol, int grow) -> float {
        int zx = gcol / (ATLAS_VERTS - 1), col = gcol % (ATLAS_VERTS - 1);
        int zy = grow / (ATLAS_VERTS - 1), row = grow % (ATLAS_VERTS - 1);
        if (zx >= ATLAS_ZONES) { zx = ATLAS_ZONES - 1; col = ATLAS_VERTS - 1; }
        if (zy >= ATLAS_ZONES) { zy = ATLAS_ZONES - 1; row = ATLAS_VERTS - 1; }
        return s_atlas_h[s_atlas_hi(zx, zy, col, row)];
    };
    float h00 = sample_grid(gx0,     gz0);
    float h10 = sample_grid(gx0 + 1, gz0);
    float h01 = sample_grid(gx0,     gz0 + 1);
    float h11 = sample_grid(gx0 + 1, gz0 + 1);
    return (h00 * (1.f - tx) + h10 * tx) * (1.f - tz)
         + (h01 * (1.f - tx) + h11 * tx) * tz;
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

    float h = base * p.amplitude;

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

// ── TerrainGen_Build ──────────────────────────────────────────────────────────

static TerrainVertex s_verts_buf[TERRAIN_VERTS];
static uint16_t      s_idx_buf  [TERRAIN_IDX];
// separate float[] nav positions (x,y,z per vert)
static float         s_nav_pos  [TERRAIN_VERTS * 3];
static int           s_nav_tri  [TERRAIN_IDX];   // same indices, cast to int
// Item 7: skirt staging buffers (4 edges × 65 × 2 verts, 4 × 64 × 6 indices)
static TerrainVertex s_skirt_v  [TERRAIN_SKIRT_VERTS];  // 520 verts
static uint16_t      s_skirt_i  [TERRAIN_SKIRT_IDX];    // 1536 indices

// See terrain_gen.h's doc comment on TerrainGen_StagingMutex(): guards the
// buffers above across the main thread's synchronous Build+Upload fallback
// (HandleTerrainStreaming/HandleFlythroughStreaming) and TerrainStreamQueue's
// worker thread, which were previously unsynchronized against each other.
std::mutex& TerrainGen_StagingMutex() {
    static std::mutex m;
    return m;
}

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
    // Task terrain-patches (milder artifact, 2026-07-17): out.loaded must go
    // false BEFORE any other field on `out` is touched, not after. Chunk array
    // slots are reused across streaming, and this is the SAME live TerrainChunk
    // sitting in the streaming grid (worker thread calls Build directly on it,
    // no private copy) — TerrainQuery::GetHeight() and other callers trust
    // `chunk.loaded` as their ONLY gate before reading chunk.heightmap.h[]
    // directly. Setting loaded=false only near the end (after heights/verts/
    // skirts were already overwritten in place) left loaded==true (stale, from
    // the previous occupant) for nearly this whole function's runtime, so a
    // concurrent GetHeight() call during that window could read a torn mix of
    // old-location and new-location heights for this chunk.
    out.loaded = false;

    float world_origin_x = coord.x * CHUNK_SIZE;
    float world_origin_z = coord.z * CHUNK_SIZE;

    // ── 1. Heights ────────────────────────────────────────────────────────────
    if (!p.force_noise && p.zone_origin_x >= 0) {
        int zx = p.zone_origin_x + coord.x;
        int zy = p.zone_origin_z + coord.z;
        // Real Kenshi zone data — always in RAM once TerrainAtlas_Load() has
        // run at startup, and every real gameplay chunk request resolves to
        // an in-bounds (zx,zy). Direct copy, no file I/O per chunk.
        const float* src = &s_atlas_h[s_atlas_zi(zx, zy) * ATLAS_ZBLOCK];
        for (int row = 0; row <= TERRAIN_GRID; ++row)
            for (int col = 0; col <= TERRAIN_GRID; ++col)
                out.heightmap.h[s_idx(col, row)] = src[row * ATLAS_VERTS + col];
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

            // UV: repeat every ~8m so texture looks natural at any terrain scale.
            // Wrapped with fmodf at a large-but-bounded modulus (2048, an exact
            // multiple of every downstream tiling multiplier used in the terrain
            // shaders — *0.125 then *4.0 — so wrapping never introduces a seam)
            // as a defensive precision margin against wx/wz being absolute world
            // coordinates (up to a few thousand metres). NOTE: a suspected
            // UV-magnitude/mip-precision bug was investigated as the cause of a
            // "flat ground" report this session and ruled out by GPU debug (forcing
            // mip0-only made no difference); the actual cause was an inherently
            // low-contrast source texture (Kenshi's own swamp mud DDS) for that
            // test zone, not a rendering bug. This wrap is kept as a reasonable,
            // harmless precision margin, not a confirmed fix for anything.
            s_verts_buf[vi].u = fmodf(wx * 0.125f, 2048.0f);
            s_verts_buf[vi].v = fmodf(wz * 0.125f, 2048.0f);

            // Task #182 (2026-07-19b): repurposed these two long-dead slots
            // (ground_id/ground_id2 — nothing has written them since the
            // per-pixel ground-selection rewrite earlier today, see
            // TerrainVertex's doc comment) as a baked chunk-local [0,1] UV
            // (local_u, local_v), smoothly interpolated by the rasterizer
            // across ANY LOD triangle exactly like position/normal already
            // are — used by terrain_forward.slang's fsMain to bilinear-
            // sample TerrainChunk::steepness_ssbo at the fragment's true
            // full-res grid location, independent of which LOD tier's
            // (sparse) triangle corners are actually being interpolated.
            s_verts_buf[vi].ground_id  = (float)col / (float)TERRAIN_GRID;  // local_u
            s_verts_buf[vi].ground_id2 = (float)row / (float)TERRAIN_GRID;  // local_v
            // morph_nx/morph_nz: placeholder here (normals aren't computed
            // until a later pass, needs neighbour heights) — the "Geomorph
            // targets" pass below overwrites these with the real parity-
            // averaged morph target once s_verts_buf's normals are fully
            // populated.
            s_verts_buf[vi].morph_nx = 0.f;
            s_verts_buf[vi].morph_nz = 0.f;

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

    // Slope/cliff splat weight is now computed PROCEDURALLY in the fragment
    // shader from the (already edge-smoothed, via TerrainAtlas_SmoothBoundaries)
    // vertex normal — matches the real Kenshi shader (terrainfp4.hlsl::main_fs:
    // `weights = smoothstep(...)` from `slope = 1.0 - normal.y`), not a per-vertex
    // baked weight. aSplat is no longer used by the atlas (real-terrain) path;
    // kept only for the force_noise fallback path above (Variant B, unrelated
    // to matching Kenshi) and for vertex-layout compatibility.

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
    // Guarded by TERRAIN_MORPH_DATA_ENABLED (terrain_chunk.h) — CPU-side computation
    // only; the GPU visual effect is a SEPARATE flag, shaders/terrain_pom.vert's own
    // TERRAIN_GEOMORPH_ENABLED (currently 0). morph_y field kept in TerrainVertex
    // ready to use once that shader flag is flipped.
#if TERRAIN_MORPH_DATA_ENABLED
    for (int row = 0; row <= TERRAIN_GRID; ++row) {
        for (int col = 0; col <= TERRAIN_GRID; ++col) {
            int vi = s_idx(col, row);
            float fy, fnx, fny, fnz;
            // Task #182i (2026-07-19): freeze the chunk border to its own
            // real height — never morph it. BuildLodIboStitched always
            // draws chunk borders at full resolution/true height regardless
            // of LOD tier (that's the whole T-junction fix); if geomorph
            // moves a border vertex, a chunk mid-ramp (lod_blend>0) renders
            // its border offset from its neighbour's border (which is
            // either still real-height at blend=0, or moved by a DIFFERENT
            // blend value — neighbouring chunks are almost never exactly
            // equidistant from the camera) and the shared edge splits open
            // again, exactly like the original T-junction gap. Confirmed by
            // code inspection, not by screenshot — the reported seams
            // persisted after the first geomorph pass despite it looking
            // correct in isolated flythrough screenshots, which is exactly
            // what you'd expect from a bug that only shows up when two
            // SPECIFIC neighbouring chunks disagree on their own blend
            // factor, not from any single chunk's own shape. Same rule
            // applies to the normal below (task #182k) for the same reason.
            bool on_border = (row == 0) || (row == TERRAIN_GRID) ||
                             (col == 0) || (col == TERRAIN_GRID);
            if (on_border) {
                fy = s_verts_buf[vi].y;
                fnx = s_verts_buf[vi].nx; fny = s_verts_buf[vi].ny; fnz = s_verts_buf[vi].nz;
            } else if ((col & 1) == 0 && (row & 1) == 0) {
                fy = s_verts_buf[vi].y;
                fnx = s_verts_buf[vi].nx; fny = s_verts_buf[vi].ny; fnz = s_verts_buf[vi].nz;
            } else if ((col & 1) != 0 && (row & 1) == 0) {
                const auto& a = s_verts_buf[s_idx(col-1, row)];
                const auto& b = s_verts_buf[s_idx(col+1, row)];
                fy  = (a.y  + b.y)  * 0.5f;
                fnx = (a.nx + b.nx) * 0.5f;
                fny = (a.ny + b.ny) * 0.5f;
                fnz = (a.nz + b.nz) * 0.5f;
            } else if ((col & 1) == 0 && (row & 1) != 0) {
                const auto& a = s_verts_buf[s_idx(col, row-1)];
                const auto& b = s_verts_buf[s_idx(col, row+1)];
                fy  = (a.y  + b.y)  * 0.5f;
                fnx = (a.nx + b.nx) * 0.5f;
                fny = (a.ny + b.ny) * 0.5f;
                fnz = (a.nz + b.nz) * 0.5f;
            } else {
                const auto& a = s_verts_buf[s_idx(col-1, row-1)];
                const auto& b = s_verts_buf[s_idx(col+1, row-1)];
                const auto& c = s_verts_buf[s_idx(col-1, row+1)];
                const auto& d = s_verts_buf[s_idx(col+1, row+1)];
                fy  = (a.y  + b.y  + c.y  + d.y)  * 0.25f;
                fnx = (a.nx + b.nx + c.nx + d.nx) * 0.25f;
                fny = (a.ny + b.ny + c.ny + d.ny) * 0.25f;
                fnz = (a.nz + b.nz + c.nz + d.nz) * 0.25f;
            }
            s_verts_buf[vi].morph_y = fy;
            // Task #182k: averaging 2-4 unit normals doesn't yield a unit
            // vector — renormalize before storing. Only x/z are kept (the
            // GPU slot has 2 free floats, not 3 — see TerrainVertex's doc
            // comment); the shader reconstructs y = sqrt(1-x^2-z^2), valid
            // since a properly renormalized heightfield normal always has
            // a positive y and this matches it exactly (up to fp rounding).
            float nlen = sqrtf(fnx*fnx + fny*fny + fnz*fnz);
            if (nlen > 1e-6f) { fnx /= nlen; fnz /= nlen; }
            s_verts_buf[vi].morph_nx = fnx;
            s_verts_buf[vi].morph_nz = fnz;
        }
    }
#endif

    // ── Task #182h: per-LOD-tier max geometric error (pixel-error LOD) ───────────
    // See TerrainChunk::lod_error's doc comment. Same "decimated-triangle-
    // corner interpolation vs real fine-res height" measurement approach as
    // md.scan_shading_mismatch() (lua_scenario_api.cpp), tracking max
    // |height delta| instead of averaging a steepness delta. Cheap: reuses
    // s_verts_buf (already fully built above), one more pass over it.
    {
        auto y_at = [](int col, int row) -> float {
            return s_verts_buf[s_idx(col, row)].y;
        };
        for (int si = 0; si < 3; ++si) {
            int step = TERRAIN_LOD_STEPS[si];
            int G = TERRAIN_GRID / step;
            float max_err = 0.f;
            for (int kr = 0; kr < G; ++kr) {
                for (int kc = 0; kc < G; ++kc) {
                    int c0 = kc*step, r0 = kr*step, c1 = c0+step, r1 = r0+step;
                    float y_bl = y_at(c0, r0), y_br = y_at(c1, r0);
                    float y_tl = y_at(c0, r1), y_tr = y_at(c1, r1);
                    for (int fr = r0; fr <= r1; ++fr) {
                        for (int fc = c0; fc <= c1; ++fc) {
                            if (fr == r0 || fr == r1 || fc == c0 || fc == c1) continue; // exact at corners/edges
                            float lx = (float)(fc - c0) / (float)step;
                            float ly = (float)(fr - r0) / (float)step;
                            float y_interp;
                            if (lx + ly <= 1.f) {
                                y_interp = y_bl*(1.f-lx-ly) + y_br*lx + y_tl*ly;
                            } else {
                                float wx = 1.f - lx, wy = 1.f - ly;
                                y_interp = y_tr*(1.f-wx-wy) + y_br*wx + y_tl*wy;
                            }
                            float err = fabsf(y_at(fc, fr) - y_interp);
                            if (err > max_err) max_err = err;
                        }
                    }
                }
            }
            out.lod_error[si] = max_err;
        }
    }

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
    // out.loaded already set false at function entry — Upload() sets it true.

    // Biome ground-texture lookup — direct biomemap.png colour → real FCS
    // Biomes entry (BiomeDef::ForColor), precise per-chunk. grass/dirt/road
    // are now genuinely per-biome (real FCS data) — previously global
    // constants shared by every biome, confirmed wrong this session (real
    // biomes have distinct grass/dirt/road textures, e.g. desert's grass
    // differs from Blister Sands' grass).
    if (p.zone_origin_x >= 0) {
        int zx = p.zone_origin_x + coord.x;
        int zy = p.zone_origin_z + coord.z;
        s_load_biomemap();

        const BiomeDef& bd = s_resolve_biome(zx, zy);
        out.ground_layers[0] = (float)bd.tex_base;
        out.ground_layers[1] = (float)bd.tex_slope;
        out.ground_layers[2] = (float)bd.tex_cliff;
        out.ground_layers[3] = (float)bd.tex_grass;
        out.ground_layers[4] = (float)bd.tex_dirt;
        out.ground_layers[5] = (float)bd.tex_road;

        // monkey_dust ARCHITECTURE NOTE (2026-07-19, task #182c/d, superseding
        // the per-chunk "neighbour heuristic" below this comment used to
        // contain): real Kenshi's blend identity is NOT "which neighbouring
        // zone is this chunk closest to" at all -- confirmed via full Ghidra
        // decompile of the render-time lookup chain (FUN_140a09630 ->
        // FUN_140a16e50 -> the SAME ground/slope/grass/dirt/road "index"
        // table terrain_gen.cpp's own s_resolve_biome() already uses) plus
        // cross-referencing real Kenshi FCS "Biomes" entries: blendmap.png's
        // R/G/B channels (each strictly binary 0/255) each independently
        // select one of a SMALL, WORLD-WIDE-CONSTANT set of up to 8 real,
        // NAMED alternate biomes (found by index: R-alone="Canyonlands
        // Crater", G-alone="Mafic Enclaves", R+G="Artery", B-alone=
        // "Canyonlands", G+B="desert" -- verified against real FCS data,
        // see re_docs/kenshi/terrain.md). This is a GLOBAL palette, not a
        // per-chunk/per-page one -- so, unlike the old code here, MD no
        // longer needs (or should have) each chunk guessing its own
        // "nearest differing neighbour": that heuristic was the actual
        // root cause of the persistent hard-seam/"square grid" bug (two
        // adjacent chunks routinely picked DIFFERENT guessed neighbours,
        // so their blend targets disagreed right at the shared edge).
        // The real fix lives in TerrainRenderer's new global comboAlternates
        // SSBO (terrain_renderer.cpp/.h) + terrain_forward.slang's fsMain,
        // which resolve the 3 single-channel alternates ONCE (BiomeRegistry::
        // ForColor(255,0,0)/(0,255,0)/(0,0,255)) and sample blendmap.png's
        // R/G/B directly as per-pixel weights toward those fixed globals --
        // no per-chunk identity data needed at all.
        // TerrainChunk::blend_layers.x REPURPOSED (task #182e, 2026-07-19,
        // FPS fix — was fully vestigial for one same-day pass, see the
        // blend_layers.x doc comment above) as a per-chunk "does this chunk
        // touch any comboAlternates biome at all" uniform flag — see
        // s_blendmap_touch's doc comment. .y/.z/.w remain genuinely unused.
        s_load_blendmap();
        out.blend_layers[0] = s_blendmap_touch(zx, zy) ? 1.f : 0.f;
        out.blend_layers[1] = out.ground_layers[1];
        out.blend_layers[2] = out.ground_layers[2];
        out.blend_layers[3] = out.ground_layers[0];

        // Dominant-weight ground selection happens PER-PIXEL in
        // terrain_forward.slang's fsMain now (2026-07-19 correction), not
        // baked per-vertex here. A per-vertex argmax + flat-interpolated
        // ground_id/ground_id2 looked right on paper ("baked into the mesh
        // vertex" per re_docs/kenshi/terrain.md:114-136) but is unstable in
        // practice: adjacent vertices on a slope routinely pick different
        // dominant layers from tiny per-vertex normal differences, and flat
        // interpolation paints each WHOLE triangle from just one vertex's
        // decision — every such disagreement became a hard triangle-shaped
        // seam (confirmed via screenshots: dense diamond/zigzag artifacts
        // exactly matching the mesh's triangulation). The fragment shader
        // already has everything it needs to make the same decision
        // per-pixel instead — the interpolated normal (smooth by
        // construction) and a live tex_overlay_mask sample — so nothing is
        // baked here anymore; ground_id/ground_id2/blend_alpha in
        // TerrainVertex (terrain_chunk.h) are unused leftover slots, same
        // status as the original dead splat[4] before this rewrite.
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
    out.heightmap_ready = true;
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
