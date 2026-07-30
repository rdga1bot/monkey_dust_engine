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

// task terrain-dedup (2026-07-29): s_load_blendmap/s_blendmap_touch REMOVED
// (compiler-confirmed unused after removing their only caller, the dead
// per-chunk blend_layers[0] write) — existed solely to gate the now-removed
// TerrainRenderer draw pipeline's combo-alternate crossfade branch.

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

bool TerrainAtlas_IsWalkableWorld(float wx, float wz) {
    if (!s_atlas_loaded) return true; // no data yet — don't falsely block movement
    // Same rule as TerrainGen_Build's per-chunk pass_grid population (see
    // that function's own "4. NavMesh (disabled) + PassGrid" comment) —
    // 100%-grade (45°) slope threshold over one PASS_CELL_SIZE step, just
    // sampled from the world-wide atlas instead of one chunk's own
    // heightmap copy.
    const float max_slope_h = PASS_CELL_SIZE * 1.0f;
    float h_c = TerrainAtlas_SampleWorld(wx, wz);
    float h_e = TerrainAtlas_SampleWorld(wx + PASS_CELL_SIZE, wz);
    float h_n = TerrainAtlas_SampleWorld(wx, wz + PASS_CELL_SIZE);
    float dh  = fabsf(h_e - h_c);
    float dhz = fabsf(h_n - h_c);
    return dh < max_slope_h && dhz < max_slope_h;
}

// Gradient-limited ramp from h_bnd (at the boundary) out toward the
// UNTOUCHED original values, capping the height CHANGE PER VERTEX at
// max_step instead of using a fixed blend fraction. Fixes a real bug in
// the previous fixed-N=15-linear-blend kernel (2026-07-26, quadtree-LOD
// terrain rewrite Phase 7 investigation — see CLAUDE_STATE.md): that
// kernel's own doc comment assumed a "worst case" raw discontinuity of
// ~22.9m/7.8m step, but real measured Kenshi fullmap zone-pair seams go
// far past that (up to 157.8m/7.2m step, confirmed via
// TerrainQuadtreeRenderer::UploadHeightmapRegion's adjacent-sample scan) —
// the fixed linear schedule only blends the CLOSEST interior vertex 6.25%
// toward the boundary average (t=1/16), leaving ~94% of an arbitrarily
// large discontinuity concentrated in that single first step: a near-
// vertical spike wall in the quadtree's real per-pixel-normal geometry
// (the old per-chunk system's own per-vertex normal baking may have
// visually absorbed this same underlying data without ever showing a
// literal geometric wall — not investigated, out of scope for this fix).
// This ramp instead walks outward one vertex at a time, moving at most
// max_step per vertex toward the true original value and stopping as soon
// as it catches up — self-scaling to the ACTUAL severity of each specific
// boundary instead of assuming one fixed worst case, with N_MAX only a
// safety cap on how far it's allowed to reach.
static void s_gradient_ramp(float* h, int stride, int n_max, float h_bnd, float max_step) {
    float prev = h_bnd;
    for (int k = 1; k <= n_max; ++k) {
        float orig = h[k * stride];
        float delta = orig - prev;
        if (fabsf(delta) <= max_step) break; // already close enough — leave the rest untouched
        prev = prev + (delta > 0.f ? max_step : -max_step);
        h[k * stride] = prev;
    }
}

void TerrainAtlas_SmoothBoundaries() {
    if (!s_atlas_loaded) return;
    // See s_gradient_ramp's doc comment for the bug this kernel replaces.
    // max_step=15m over one ~7.2m atlas step is still a steep (~64% grade,
    // ~32.6°) per-vertex ramp — real, legitimately steep Kenshi cliffs away
    // from a zone SEAM keep their actual shape untouched (this function only
    // ever touches the N_MAX=40 vertices nearest a zone boundary); this only
    // bounds how fast an AUTHORING-ARTIFACT seam is allowed to resolve.
    constexpr int N_MAX = 40;
    constexpr float kMaxStepM = 15.f;

    // X-direction seams: average col=ATLAS_VERTS-1(128) of zone(zx) with col=0 of zone(zx+1),
    // then ramp outward from that shared average on each side. This ensures
    // the shared boundary vertex is identical in both chunks.
    for (int zy = 0; zy < ATLAS_ZONES; ++zy) {
        for (int zx = 0; zx < ATLAS_ZONES - 1; ++zx) {
            for (int row = 0; row < ATLAS_VERTS; ++row) {
                float& h_left  = s_atlas_h[s_atlas_hi(zx,   zy, ATLAS_VERTS-1, row)]; // col=128 of A
                float& h_right = s_atlas_h[s_atlas_hi(zx+1, zy, 0,             row)]; // col=0   of B
                float h_bnd = (h_left + h_right) * 0.5f;  // average → shared vertex
                h_left  = h_bnd;
                h_right = h_bnd;
                s_gradient_ramp(&s_atlas_h[s_atlas_hi(zx+1, zy, 0, row)], 1, N_MAX, h_bnd, kMaxStepM);
                // Left side walks toward DECREASING col from ATLAS_VERTS-1 —
                // stride -1 from that same base index.
                s_gradient_ramp(&s_atlas_h[s_atlas_hi(zx, zy, ATLAS_VERTS-1, row)], -1, N_MAX, h_bnd, kMaxStepM);
            }
        }
    }

    // Z-direction seams: average row=64 of zone(zy) with row=0 of zone(zy+1).
    for (int zy = 0; zy < ATLAS_ZONES - 1; ++zy) {
        for (int zx = 0; zx < ATLAS_ZONES; ++zx) {
            for (int col = 0; col < ATLAS_VERTS; ++col) {
                float& h_bot = s_atlas_h[s_atlas_hi(zx, zy,   col, ATLAS_VERTS-1)]; // row=128 of A
                float& h_top = s_atlas_h[s_atlas_hi(zx, zy+1, col, 0            )]; // row=0   of B
                float h_bnd = (h_bot + h_top) * 0.5f;
                h_bot = h_bnd;
                h_top = h_bnd;
                // row stride within one (zx,zy) zone block is ATLAS_VERTS
                // (row-major, see s_atlas_hi) — same trick as the X-loop,
                // just with that stride instead of 1.
                s_gradient_ramp(&s_atlas_h[s_atlas_hi(zx, zy+1, col, 0)], ATLAS_VERTS, N_MAX, h_bnd, kMaxStepM);
                s_gradient_ramp(&s_atlas_h[s_atlas_hi(zx, zy, col, ATLAS_VERTS-1)], -ATLAS_VERTS, N_MAX, h_bnd, kMaxStepM);
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

// task terrain-dedup (2026-07-29): s_verts_buf/s_idx_buf/s_skirt_v/s_skirt_i
// (fed only the now-removed chunk.vbo/ibo/skirt_vbo/skirt_ibo GPU buffers)
// and s_nav_pos/s_nav_tri (computed but never exposed via any accessor —
// per-chunk Recast navmesh has been disabled since before this session,
// NPC pathfinding uses the NavSystem singleton instead, see this
// function's own "4. NavMesh (disabled)" comment below) all REMOVED —
// verified zero readers, same investigation as terrain_renderer.h's class
// doc comment.

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

    // task terrain-dedup (2026-07-29): removed here (all verified zero
    // readers, same investigation as terrain_renderer.h class doc comment):
    // vertex/index building (fed only chunk.vbo/ibo, now removed), cross-
    // chunk normal stitching + geomorph targets (fed only s_verts_buf
    // fields uploaded to the same dead vbo), per-LOD-tier lod_error
    // (out.lod_error field removed), and skirt geometry (fed only
    // chunk.skirt_vbo/skirt_ibo, also removed). heights (step 1 above) and
    // pass_grid/coord/center (below) are unaffected -- those are the parts
    // physics/TerrainQuery/AI actually read.
    out.coord    = coord;
    out.center_x = p.world_offset_x + coord.x * CHUNK_SIZE + CHUNK_SIZE * 0.5f;
    out.center_z = p.world_offset_z + coord.z * CHUNK_SIZE + CHUNK_SIZE * 0.5f;
    // out.loaded already set false at function entry — Upload() sets it true.

    // Ensures md_biomemap.png is loaded (lazy, tried-once) for external
    // TerrainGen_ResolveBiome() callers (e.g. editor's s_build_zone_
    // ground_layers -- feeds Granite's live zoneGroundLayers SSBO). The
    // per-chunk ground_layers/blend_layers writes that used to follow here
    // were removed (task terrain-dedup, 2026-07-29) -- those fields fed
    // only the now-removed TerrainRenderer draw pipeline; s_load_blendmap/
    // s_blendmap_touch existed solely to compute blend_layers[0], now gone
    // too. Per-pixel ground selection in Granite's terrain_patch.frag reads
    // the SSBO directly, no per-chunk biome data needed here.
    if (p.zone_origin_x >= 0) s_load_biomemap();

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

// task terrain-dedup (2026-07-29): TerrainGen_StagedVerts/StagedIndices/
// StagedSkirtVerts/StagedSkirtIndices REMOVED — their only caller
// (terrain_upload.cpp's s_upload_core) no longer needs staged mesh data,
// see that file's own updated comment.

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
