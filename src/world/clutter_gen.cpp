// ClutterGen — bakes dense ground clutter (pebbles/small rocks/small plants) into
// one static merged vertex/index buffer per chunk. No GPU calls; safe on a worker
// thread. See engine/include/monkey_dust/world/clutter_gen.h for the rationale.
#include <monkey_dust/world/clutter_gen.h>

// cgltf.h lives alongside prop_mesh.cpp; CGLTF_IMPLEMENTATION is provided once
// by engine/src/render/cgltf_impl.cpp (compiled separately).
#include "../render/cgltf.h"

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

static constexpr int kSourceCount = 3;
static ClutterSource s_sources[kSourceCount];
static bool          s_sources_ready = false;

static bool load_source_glb(const char* path, ClutterSource& out) {
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
    for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
        if (prim->attributes[ai].type == cgltf_attribute_type_position) pos_acc  = prim->attributes[ai].data;
        if (prim->attributes[ai].type == cgltf_attribute_type_normal)   norm_acc = prim->attributes[ai].data;
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
        float p[3] = {0.f, 0.f, 0.f};
        float n[3] = {0.f, 1.f, 0.f};
        cgltf_accessor_read_float(pos_acc,  i, p, 3);
        cgltf_accessor_read_float(norm_acc, i, n, 3);
        out.verts[i] = { p[0], p[1], p[2], n[0], n[1], n[2] };
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
    ok = load_source_glb("game/data/props/rock_round.glb",  s_sources[0]) && ok;
    ok = load_source_glb("game/data/props/yucca_small.glb", s_sources[1]) && ok;
    ok = load_source_glb("game/data/props/shrub.glb",       s_sources[2]) && ok;
    s_sources_ready = ok;
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
            if (p01 > fill_prob) continue;

            float lx = (gx + ((h >> 8) & 0x7F) / 127.f) * CELL_SIZE;
            float lz = (gy + ((h >> 16) & 0x7F) / 127.f) * CELL_SIZE;
            lx = lx < 0.f ? 0.f : (lx > CHUNK_SIZE ? CHUNK_SIZE : lx);
            lz = lz < 0.f ? 0.f : (lz > CHUNK_SIZE ? CHUNK_SIZE : lz);

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

            float yaw   = ((h >> 4)  & 0xFF) / 255.f * 6.28318f;
            float scale = 0.7f + ((h >> 12) & 0xFF) / 255.f * 0.6f;  // 0.7..1.3
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
                s_clutter_v[s_clutter_vc++] = { wx + rx, wy + py, wz + rz, nx, sv.ny, nz };
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
