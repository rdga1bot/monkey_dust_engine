// ClutterGen — bakes dense ground clutter (pebbles/small rocks/small plants) into
// one static merged vertex/index buffer per chunk. No GPU calls; safe on a worker
// thread. See engine/include/monkey_dust/world/clutter_gen.h for the rationale.
#include <monkey_dust/world/clutter_gen.h>

// cgltf.h lives alongside prop_mesh.cpp; CGLTF_IMPLEMENTATION is provided once
// by engine/src/render/cgltf_impl.cpp (compiled separately).
#include "../render/cgltf.h"
// stb_image implementation lives in engine/src/vendor/stb_image_impl.cpp
// (its own TU) — this file only needs the decode API, same #include pattern
// terrain_gen.cpp already uses for its s_load_areasmap().
#include "stb_image.h"

#include <cstdio>
#include <cstring>
#include <cmath>

// ── CPU-only source mesh cache (loaded once, read-only after that) ───────────
// Small, cheap meshes only — this is baked hundreds of times per chunk.
struct ClutterSource {
    PropVertex verts[512];
    uint16_t   idx[1536];
    int        vert_count = 0;
    int        idx_count  = 0;
    bool       loaded     = false;
};

// KEN-CLUTTER v3: 21 sources instead of 3 -- 3 original small props + 6 v2
// low-poly meshes from tmp_/kenshi/data/foliage/{grass,rocks}/ (the OLDER,
// smaller base-game asset set) + 12 v3 low-poly meshes from
// tmp_/kenshi/data/newland/Assets/{Plants,Rocks}/ -- a vastly larger,
// never-before-explored asset library specific to the CURRENT "newland" map
// (hundreds of plant/rock variants; user pointed out this directory existed
// and asked for genuine variety -- "тисячі різноманітних об'єктів"). See
// ClutterGen_LoadSources's comment for why the first attempt at variety,
// reusing Tier-1 big-prop GLBs, failed the budget check.
static constexpr int kSourceCount = 21;
static ClutterSource s_sources[kSourceCount];
static bool          s_sources_ready = false;

// ── CPU-side grass/dirt/road density mask (KEN-CLUTTER v2) ───────────────────
// Real Kenshi's own grass density map drives 3D grass-clutter PLACEMENT
// density, not just ground-texture blend weight (RE-confirmed: kenshi_terrain
// sweep block 00182, "density map drives grass placement density per-pixel").
// We already stitch this exact map as md_overlay_mask.png (R=grass,G=secondary,
// same file the GPU shader samples as tex_overlay_mask) -- ClutterGen_Build
// previously never read it at all, placing clutter by biome-fixed probability
// only, with zero connection to where the ground is actually painted grass.
// Loaded once here (CPU-only, worker-thread-safe, mirrors terrain_gen.cpp's
// s_load_areasmap pattern) since clutter baking has no GPU access.
static uint8_t* s_grass_density   = nullptr;
static int      s_gd_w = 0, s_gd_h = 0;
static bool     s_gd_tried = false;

static void s_load_grass_density() {
    if (s_gd_tried) return;
    s_gd_tried = true;
    int comp;
    s_grass_density = stbi_load("game/data/textures/md_overlay_mask.png", &s_gd_w, &s_gd_h, &comp, 4);
    if (!s_grass_density)
        fprintf(stderr, "[ClutterGen] md_overlay_mask.png not found — density-driven placement disabled (biome-only fill_prob)\n");
}

bool ClutterGen_GetGrassDensityRGBA(const uint8_t** out_pixels, int* out_w, int* out_h) {
    if (!s_grass_density) return false;
    *out_pixels = s_grass_density;
    *out_w = s_gd_w;
    *out_h = s_gd_h;
    return true;
}

// World size the stitched overlay atlas spans 1:1 (matches KENSHI_WORLD_SIZE
// in npc_render.cpp/editor_world_3d_sdlgpu.cpp: 64 zones * CHUNK_SIZE).
static constexpr float kOverlayWorldSizeM = 64.f * CHUNK_SIZE;

// grass_w = max(R,G) -- same convention as terrain_pom.frag/terrain_forward.frag
// ("secondary channel merges into grass, matches real shader"). Returns 1.0
// (no suppression) if the mask failed to load, so clutter still places by
// biome fill_prob alone rather than silently vanishing.
static float s_grass_density_at(float wx, float wz) {
    if (!s_grass_density) return 1.f;
    float u = wx / kOverlayWorldSizeM, v = wz / kOverlayWorldSizeM;
    int px = (int)(u * (float)s_gd_w), py = (int)(v * (float)s_gd_h);
    if (px < 0) px = 0; if (px >= s_gd_w) px = s_gd_w - 1;
    if (py < 0) py = 0; if (py >= s_gd_h) py = s_gd_h - 1;
    const uint8_t* p = s_grass_density + ((size_t)py * (size_t)s_gd_w + (size_t)px) * 4;
    float r = p[0] / 255.f, g = p[1] / 255.f;
    return r > g ? r : g;
}

static bool load_source_glb(const char* path, ClutterSource& out, float layer) {
    cgltf_options opts = {};
    cgltf_data*   data = nullptr;
    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success) {
        fprintf(stderr, "[ClutterGen] cgltf_parse_file failed: %s\n", path);
        return false;
    }
    if (cgltf_load_buffers(&opts, data, path) != cgltf_result_success) {
        fprintf(stderr, "[ClutterGen] cgltf_load_buffers failed: %s\n", path);
        cgltf_free(data);
        return false;
    }

    cgltf_primitive* prim = nullptr;
    for (cgltf_size mi = 0; mi < data->meshes_count && !prim; ++mi) {
        for (cgltf_size pi = 0; pi < data->meshes[mi].primitives_count && !prim; ++pi) {
            cgltf_primitive* p = &data->meshes[mi].primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            bool has_pos = false, has_norm = false;
            for (cgltf_size ai = 0; ai < p->attributes_count; ++ai) {
                if (p->attributes[ai].type == cgltf_attribute_type_position) has_pos  = true;
                if (p->attributes[ai].type == cgltf_attribute_type_normal)   has_norm = true;
            }
            if (has_pos && has_norm && p->indices) prim = p;
        }
    }
    if (!prim) {
        fprintf(stderr, "[ClutterGen] No valid primitive: %s\n", path);
        cgltf_free(data);
        return false;
    }

    cgltf_accessor* pos_acc  = nullptr;
    cgltf_accessor* norm_acc = nullptr;
    cgltf_accessor* uv_acc   = nullptr;
    for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
        if (prim->attributes[ai].type == cgltf_attribute_type_position) pos_acc  = prim->attributes[ai].data;
        if (prim->attributes[ai].type == cgltf_attribute_type_normal)   norm_acc = prim->attributes[ai].data;
        if (prim->attributes[ai].type == cgltf_attribute_type_texcoord) uv_acc   = prim->attributes[ai].data;
    }

    cgltf_size vert_count = pos_acc->count;
    cgltf_size idx_count  = prim->indices->count;
    if (vert_count == 0 || (int)vert_count > 512 || (int)idx_count > 1536) {
        fprintf(stderr, "[ClutterGen] source too large for clutter budget (%zu v / %zu i): %s\n",
                vert_count, idx_count, path);
        cgltf_free(data);
        return false;
    }

    for (cgltf_size i = 0; i < vert_count; ++i) {
        float p[3]  = {0.f, 0.f, 0.f};
        float n[3]  = {0.f, 1.f, 0.f};
        float uv[2] = {0.f, 0.f};
        cgltf_accessor_read_float(pos_acc,  i, p, 3);
        cgltf_accessor_read_float(norm_acc, i, n, 3);
        if (uv_acc) cgltf_accessor_read_float(uv_acc, i, uv, 2);
        out.verts[i] = { p[0], p[1], p[2], n[0], n[1], n[2], uv[0], uv[1], layer };
    }
    for (cgltf_size i = 0; i < idx_count; ++i)
        out.idx[i] = (uint16_t)cgltf_accessor_read_index(prim->indices, i);

    out.vert_count = (int)vert_count;
    out.idx_count  = (int)idx_count;
    out.loaded     = true;
    cgltf_free(data);
    fprintf(stdout, "[ClutterGen] loaded source %d v / %d i: %s\n", out.vert_count, out.idx_count, path);
    return true;
}

bool ClutterGen_LoadSources() {
    bool ok = true;
    // layer: 0=rock diffuse, 1=vegetation atlas (PropTexShared).
    //
    // KEN-CLUTTER v2 (indices 3-7): first attempt reused the Tier-1 big-prop
    // GLBs (rock_01/rock_formation/canyon_rock/bones/ruin_junk) — all 5
    // failed load_source_glb's budget check (960-5535 verts each; Tier-1
    // meshes are meant for individual PropRenderer draws, not hundreds of
    // merged instances per chunk). Real fix: real Kenshi ships genuinely
    // low-poly ground-clutter meshes in a SEPARATE, never-before-converted
    // asset set (tmp_/kenshi/data/foliage/{grass,rocks}/*.mesh — 12-98 verts
    // each, confirmed by direct measurement) that the Tier-1 system never
    // used. Converted via tools/md_convert_static.py (new: OGRE mesh.xml ->
    // GLB for simple static props, no skeleton/skinning, unlike md_convert.py).
    ok = load_source_glb("game/data/props/rock_round.glb",     s_sources[0], 0.f) && ok;
    ok = load_source_glb("game/data/props/yucca_small.glb",    s_sources[1], 1.f) && ok;
    ok = load_source_glb("game/data/props/shrub.glb",          s_sources[2], 1.f) && ok;
    ok = load_source_glb("game/data/props/grass_green.glb",    s_sources[3], 1.f) && ok;
    ok = load_source_glb("game/data/props/yel_sub_grass.glb",  s_sources[4], 1.f) && ok;
    ok = load_source_glb("game/data/props/red_flowers.glb",    s_sources[5], 1.f) && ok;
    ok = load_source_glb("game/data/props/shrubs_grass.glb",   s_sources[6], 1.f) && ok;
    ok = load_source_glb("game/data/props/grass_yellow.glb",   s_sources[7], 1.f) && ok;
    ok = load_source_glb("game/data/props/rock_lowpoly.glb",   s_sources[8], 0.f) && ok;
    // KEN-CLUTTER v3: tmp_/kenshi/data/newland/Assets/{Plants,Rocks}/ --
    // current-map asset library, 12-288 verts each (confirmed by direct
    // measurement, all well under budget).
    ok = load_source_glb("game/data/props/phantom_grass.glb",     s_sources[9],  1.f) && ok;
    ok = load_source_glb("game/data/props/spore01.glb",           s_sources[10], 1.f) && ok;
    ok = load_source_glb("game/data/props/spore02.glb",           s_sources[11], 1.f) && ok;
    ok = load_source_glb("game/data/props/tree_beard.glb",        s_sources[12], 1.f) && ok;
    ok = load_source_glb("game/data/props/urchin_brush.glb",      s_sources[13], 1.f) && ok;
    ok = load_source_glb("game/data/props/knee_leaf.glb",         s_sources[14], 1.f) && ok;
    ok = load_source_glb("game/data/props/flat_slab01.glb",       s_sources[15], 0.f) && ok;
    ok = load_source_glb("game/data/props/flat_slab06.glb",       s_sources[16], 0.f) && ok;
    ok = load_source_glb("game/data/props/boulder01.glb",         s_sources[17], 0.f) && ok;
    ok = load_source_glb("game/data/props/boulder03.glb",         s_sources[18], 0.f) && ok;
    ok = load_source_glb("game/data/props/smooth_boulder02.glb",  s_sources[19], 0.f) && ok;
    ok = load_source_glb("game/data/props/boulder_small15.glb",   s_sources[20], 0.f) && ok;
    // Individual failures are tolerated (ClutterGen_Build already skips any
    // src.loaded==false pick) — one bad/oversized mesh shouldn't silently
    // disable clutter entirely like a strict all-or-nothing gate would.
    // Only refuse to build if EVERY source failed.
    s_sources_ready = false;
    for (int i = 0; i < kSourceCount; ++i) s_sources_ready |= s_sources[i].loaded;
    s_load_grass_density();
    return ok;
}

// ── Placement + bake ──────────────────────────────────────────────────────────
static PropVertex s_clutter_v[CLUTTER_MAX_VERTS];
static uint16_t   s_clutter_i[CLUTTER_MAX_IDX];
static int        s_clutter_vc = 0;
static int        s_clutter_ic = 0;

static uint32_t hashc(int a, int b, int c, int d) {
    uint32_t h = 2166136261u;
    auto mix = [&](int v) { h ^= (uint32_t)v; h *= 16777619u; h ^= (h >> 13); h *= 0x45d9f3bu; };
    mix(a); mix(b); mix(c); mix(d);
    return h;
}

void ClutterGen_Build(TerrainChunk& chunk, const char* biome_slug) {
    s_clutter_vc = 0;
    s_clutter_ic = 0;
    if (!s_sources_ready) return;

    // Biome density: rocky/desert biomes denser, scrub sparser — mirrors the
    // BiomePropPalette approach in game/src/world/prop_gen.cpp without needing
    // a full separate table for this first pass.
    float fill_prob = 0.55f;
    if (biome_slug) {
        if (strstr(biome_slug, "canyon") || strstr(biome_slug, "desert") || strstr(biome_slug, "high_rock"))
            fill_prob = 0.80f;
        else if (strstr(biome_slug, "scrub"))
            fill_prob = 0.40f;
    }

    const float ox = chunk.center_x - CHUNK_SIZE * 0.5f;
    const float oz = chunk.center_z - CHUNK_SIZE * 0.5f;

    static constexpr int   N_CELLS   = 24;
    static constexpr float CELL_SIZE = CHUNK_SIZE / (float)N_CELLS;

    for (int gy = 0; gy < N_CELLS; ++gy) {
        for (int gx = 0; gx < N_CELLS; ++gx) {
            uint32_t h = hashc(chunk.coord.x * N_CELLS + gx,
                               chunk.coord.z * N_CELLS + gy, 0xC1D7u, 0x7EA5u);
            float p01 = (float)(h & 0xFFFF) / 65535.f;

            float lx = (gx + ((h >> 8) & 0x7F) / 127.f) * CELL_SIZE;
            float lz = (gy + ((h >> 16) & 0x7F) / 127.f) * CELL_SIZE;
            lx = lx < 0.f ? 0.f : (lx > CHUNK_SIZE ? CHUNK_SIZE : lx);
            lz = lz < 0.f ? 0.f : (lz > CHUNK_SIZE ? CHUNK_SIZE : lz);

            // Density-driven placement (KEN-CLUTTER v2): real Kenshi's grass
            // density map drives 3D clutter placement density, not just
            // ground-texture blend weight (see s_grass_density_at's header
            // comment). Floor of 0.25 keeps bare/dirt patches lightly
            // scattered (real ground still has occasional rocks/debris off
            // the painted grass mask) rather than a hard on/off cutoff.
            float density  = s_grass_density_at(ox + lx, oz + lz);
            float eff_prob = fill_prob * (0.25f + 0.75f * density);
            if (p01 > eff_prob) continue;

            // Slope guard — clutter looks wrong floating on cliff faces.
            float h0 = chunk.SampleHeight(lx, lz);
            float h1 = chunk.SampleHeight(lx + TERRAIN_STEP, lz);
            float h2 = chunk.SampleHeight(lx, lz + TERRAIN_STEP);
            float slope = sqrtf((h1 - h0) * (h1 - h0) + (h2 - h0) * (h2 - h0)) / TERRAIN_STEP;
            if (slope > 0.9f) continue;

            int src_idx = (int)((h >> 24) % (uint32_t)kSourceCount);
            const ClutterSource& src = s_sources[src_idx];
            if (!src.loaded) continue;
            if (s_clutter_vc + src.vert_count > CLUTTER_MAX_VERTS) return;  // budget hit
            if (s_clutter_ic + src.idx_count  > CLUTTER_MAX_IDX)  return;

            // Per-source base scale — these are raw OGRE mesh units, each mesh
            // needs its own correction, not one shared multiplier (without
            // per-mesh scale, rock_round/yucca_small/shrub rendered at ~1.0x —
            // giant, overlapping, mistaken for broken geometry). Indices 3-8
            // (KEN-CLUTTER v2) are new low-poly Kenshi grass/rock meshes
            // (tools/md_convert_static.py's printed raw AABB Y-extent, target
            // final height ~0.25-0.5m for grass/flowers, ~0.25m for the rock —
            // measured, not guessed, same practice as the original 3).
            static constexpr float kSourceBaseScale[kSourceCount] = {
                0.05f,    // rock_round:     ~5.7m raw  -> ~0.3m pebble/small rock
                0.03f,    // yucca_small:    ~25.2m raw -> ~0.75m small plant
                0.012f,   // shrub:          ~43.4m raw -> ~0.5m shrub
                0.067f,   // grass_green:    Y=5.963 raw -> ~0.4m tuft
                0.073f,   // yel_sub_grass:  Y=4.126 raw -> ~0.3m tuft
                0.067f,   // red_flowers:    Y=3.754 raw -> ~0.25m flower clump
                0.054f,   // shrubs_grass:   Y=9.280 raw -> ~0.5m clump
                0.057f,   // grass_yellow:   Y=6.130 raw -> ~0.35m tuft
                0.044f,   // rock_lowpoly:   Y=5.662 raw -> ~0.25m small rock
                0.0243f,  // phantom_grass:  Y=16.451 raw  -> ~0.4m tuft
                0.00197f, // spore01:        Y=101.488 raw -> ~0.2m ground fungus
                0.00176f, // spore02:        Y=113.721 raw -> ~0.2m ground fungus
                0.00052f, // tree_beard:     Y=1152.807 raw -> ~0.6m hanging moss clump
                0.00629f, // urchin_brush:   Y=47.688 raw -> ~0.3m spiky brush
                0.0271f,  // knee_leaf:      Y=9.217 raw -> ~0.25m leaf clump
                0.00839f, // flat_slab01:    Y=17.872 raw -> ~0.15m flat rock slab
                0.00671f, // flat_slab06:    Y=17.872 raw -> ~0.12m flat rock slab
                0.0372f,  // boulder01:      Y=8.058 raw -> ~0.3m small boulder
                0.0676f,  // boulder03:      Y=2.959 raw -> ~0.2m small boulder
                0.0117f,  // smooth_boulder02: Y=29.851 raw -> ~0.35m rounded rock
                0.0868f,  // boulder_small15: Y=1.728 raw -> ~0.15m pebble
            };
            float yaw   = ((h >> 4)  & 0xFF) / 255.f * 6.28318f;
            float scale = kSourceBaseScale[src_idx] * (0.7f + ((h >> 12) & 0xFF) / 255.f * 0.6f);  // ±30% jitter
            float wx = ox + lx, wz = oz + lz, wy = h0 - 0.03f;
            float cy = cosf(yaw), sy = sinf(yaw);

            int base_v = s_clutter_vc;
            for (int vi = 0; vi < src.vert_count; ++vi) {
                const PropVertex& sv = src.verts[vi];
                float px = sv.x * scale, py = sv.y * scale, pz = sv.z * scale;
                float rx =  px * cy + pz * sy;
                float rz = -px * sy + pz * cy;
                float nx =  sv.nx * cy + sv.nz * sy;
                float nz = -sv.nx * sy + sv.nz * cy;
                s_clutter_v[s_clutter_vc++] = { wx + rx, wy + py, wz + rz, nx, sv.ny, nz, sv.u, sv.v, sv.layer };
            }
            for (int ii = 0; ii < src.idx_count; ++ii)
                s_clutter_i[s_clutter_ic++] = (uint16_t)(base_v + src.idx[ii]);
        }
    }
}

const PropVertex* ClutterGen_StagedVerts()   { return s_clutter_v; }
const uint16_t*   ClutterGen_StagedIndices() { return s_clutter_i; }
int               ClutterGen_StagedVertCount()  { return s_clutter_vc; }
int               ClutterGen_StagedIndexCount() { return s_clutter_ic; }
