#include <monkey_dust/render/material_desc.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>
#include <cstdlib>

// ── strstr-based JSON helpers ─────────────────────────────────────────────────

static bool read_str(const char* json, const char* key, char* out, int out_sz) {
    const char* p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    const char* qs = strchr(p, '"');
    if (!qs) return false;
    const char* qe = strchr(qs + 1, '"');
    if (!qe) return false;
    int len = (int)(qe - qs - 1);
    if (len <= 0 || len >= out_sz) return false;
    memcpy(out, qs + 1, (size_t)len);
    out[len] = '\0';
    return true;
}

static bool read_bool(const char* json, const char* key, bool def) {
    const char* p = strstr(json, key);
    if (!p) return def;
    p += strlen(key);
    while (*p && *p != ':') ++p;
    if (!*p) return def;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (strncmp(p, "true",  4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return def;
}

static int read_int(const char* json, const char* key, int def) {
    const char* p = strstr(json, key);
    if (!p) return def;
    p += strlen(key);
    while (*p && *p != ':') ++p;
    if (!*p) return def;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') return def;
    return (int)strtol(p, nullptr, 10);
}

// Parse "shader_features": ["SF_Skinned", "SF_Shadows", ...]
static uint32_t parse_shader_features(const char* json) {
    const char* p = strstr(json, "\"shader_features\"");
    if (!p) return SF_None;
    const char* arr_s = strchr(p, '[');
    const char* arr_e = strchr(p, ']');
    if (!arr_s || !arr_e || arr_s >= arr_e) return SF_None;

    uint32_t feats = SF_None;
    const char* cur = arr_s;
    while (cur < arr_e) {
        const char* qs = strchr(cur, '"');
        if (!qs || qs >= arr_e) break;
        const char* qe = strchr(qs + 1, '"');
        if (!qe || qe >= arr_e) break;
        int len = (int)(qe - qs - 1);
        if (len > 0 && len < 24) {
            char feat[24] = {};
            memcpy(feat, qs + 1, (size_t)len);
            if (strcmp(feat, "SF_Skinned")   == 0) feats |= SF_Skinned;
            if (strcmp(feat, "SF_Shadows")   == 0) feats |= SF_Shadows;
            if (strcmp(feat, "SF_AlphaTest") == 0) feats |= SF_AlphaTest;
        }
        cur = qe + 1;
    }
    return feats;
}

// ── MaterialDesc::LoadFromJson ────────────────────────────────────────────────

bool MaterialDesc::LoadFromJson(MaterialDesc& out, const char* json) noexcept {
    if (!json) return false;
    memset(&out, 0, sizeof(out));

    if (!read_str(json, "\"name\"", out.name, MD_MAT_NAME_LEN)) {
        MD_LOG(MD_LOG_WARNING, "MaterialDesc: missing \"name\"");
        return false;
    }

    // Resolve parent — seed defaults before overriding with own fields.
    bool has_parent = read_str(json, "\"parent\"", out.parent_name, MD_MAT_NAME_LEN);
    if (has_parent) {
        const MaterialDesc* parent = MaterialTypeRegistry::Get().Find(out.parent_name);
        if (!parent) {
            MD_LOG(MD_LOG_WARNING, "MaterialDesc: parent '%s' not in registry", out.parent_name);
            has_parent = false;
        } else {
            // Inherit all shader fields from parent as starting defaults.
            memcpy(out.vert_shader, parent->vert_shader, MD_MAT_SHADER_LEN);
            memcpy(out.frag_shader, parent->frag_shader, MD_MAT_SHADER_LEN);
            out.blend           = parent->blend;
            out.cull            = parent->cull;
            out.topology        = parent->topology;
            out.depth_write     = parent->depth_write;
            out.depth_test      = parent->depth_test;
            out.shader_features = parent->shader_features;
            out.version         = parent->version;
        }
    }

    if (!has_parent) {
        // Without a parent, vert_shader + frag_shader are required.
        out.blend       = MatBlend::Opaque;
        out.cull        = MatCull::Back;
        out.topology    = GpuTopology::TRIANGLES;
        out.depth_write = true;
        out.depth_test  = true;
        out.version     = 1;
    }

    // Override with whatever the current JSON declares.
    read_str(json, "\"vert_shader\"", out.vert_shader, MD_MAT_SHADER_LEN);
    read_str(json, "\"frag_shader\"", out.frag_shader, MD_MAT_SHADER_LEN);

    if (!out.vert_shader[0] || !out.frag_shader[0]) {
        MD_LOG(MD_LOG_WARNING, "MaterialDesc: '%s' has no vert/frag shader (and no parent)", out.name);
        return false;
    }

    char tmp[16] = {};
    if (read_str(json, "\"blend\"", tmp, sizeof(tmp))) {
        if      (strcmp(tmp, "alpha")    == 0) out.blend = MatBlend::Alpha;
        else if (strcmp(tmp, "additive") == 0) out.blend = MatBlend::Additive;
        else                                   out.blend = MatBlend::Opaque;
    }
    if (read_str(json, "\"cull\"", tmp, sizeof(tmp))) {
        if      (strcmp(tmp, "front") == 0) out.cull = MatCull::Front;
        else if (strcmp(tmp, "none")  == 0) out.cull = MatCull::None;
        else                                out.cull = MatCull::Back;
    }
    if (read_str(json, "\"topology\"", tmp, sizeof(tmp))) {
        if      (strcmp(tmp, "lines")  == 0) out.topology = GpuTopology::LINES;
        else if (strcmp(tmp, "points") == 0) out.topology = GpuTopology::POINTS;
        else                                 out.topology = GpuTopology::TRIANGLES;
    }

    // read_bool preserves parent default when key is absent (def param unused here —
    // we pass the already-inherited value as def).
    const char* has_dw = strstr(json, "\"depth_write\"");
    if (has_dw) out.depth_write = read_bool(json, "\"depth_write\"", true);
    const char* has_dt = strstr(json, "\"depth_test\"");
    if (has_dt) out.depth_test  = read_bool(json, "\"depth_test\"",  true);

    // shader_features: OR own flags onto inherited base (additive).
    out.shader_features |= parse_shader_features(json);

    int ver = read_int(json, "\"version\"", -1);
    if (ver > 0) out.version = (uint8_t)ver;

    return true;
}

// ── MaterialDesc::BuildPipelineDesc ──────────────────────────────────────────

GpuPipeline::Desc MaterialDesc::BuildPipelineDesc(const MaterialDesc& md,
                                                   const GpuVertexLayout& layout) noexcept {
    GpuPipeline::Desc d;
    d.vert_path       = md.vert_shader;
    d.frag_path       = md.frag_shader;
    d.layout          = layout;
    d.shader_features = md.shader_features;

    d.raster.topology    = md.topology;
    d.raster.depth_test  = md.depth_test;
    d.raster.depth_write = md.depth_write;
    d.raster.cull_back   = (md.cull == MatCull::Back);

    if (md.blend == MatBlend::Alpha) {
        d.raster.blend_enable = true;
        d.raster.src_factor   = GpuBlendFactor::SRC_ALPHA;
        d.raster.dst_factor   = GpuBlendFactor::ONE_MINUS_SRC_ALPHA;
    } else if (md.blend == MatBlend::Additive) {
        d.raster.blend_enable = true;
        d.raster.src_factor   = GpuBlendFactor::SRC_ALPHA;
        d.raster.dst_factor   = GpuBlendFactor::ONE;
    }

    return d;
}
