#include <monkey_dust/render/material_desc.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

// Minimal strstr-based JSON reader (same pattern as bt_json_loader).
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

bool MaterialDesc::LoadFromJson(MaterialDesc& out, const char* json) noexcept {
    if (!json) return false;
    memset(&out, 0, sizeof(out));
    out.blend       = MatBlend::Opaque;
    out.cull        = MatCull::Back;
    out.topology    = GpuTopology::TRIANGLES;
    out.depth_write = true;
    out.depth_test  = true;

    if (!read_str(json, "\"name\"",        out.name,        MD_MAT_NAME_LEN))   return false;
    if (!read_str(json, "\"vert_shader\"", out.vert_shader, MD_MAT_SHADER_LEN)) return false;
    if (!read_str(json, "\"frag_shader\"", out.frag_shader, MD_MAT_SHADER_LEN)) return false;

    char tmp[16] = {};
    if (read_str(json, "\"blend\"", tmp, sizeof(tmp))) {
        if      (strcmp(tmp, "alpha")    == 0) out.blend = MatBlend::Alpha;
        else if (strcmp(tmp, "additive") == 0) out.blend = MatBlend::Additive;
    }
    if (read_str(json, "\"cull\"", tmp, sizeof(tmp))) {
        if      (strcmp(tmp, "front") == 0) out.cull = MatCull::Front;
        else if (strcmp(tmp, "none")  == 0) out.cull = MatCull::None;
    }
    if (read_str(json, "\"topology\"", tmp, sizeof(tmp))) {
        if      (strcmp(tmp, "lines")  == 0) out.topology = GpuTopology::LINES;
        else if (strcmp(tmp, "points") == 0) out.topology = GpuTopology::POINTS;
    }
    out.depth_write = read_bool(json, "\"depth_write\"", true);
    out.depth_test  = read_bool(json, "\"depth_test\"",  true);
    return true;
}

GpuPipeline::Desc MaterialDesc::BuildPipelineDesc(const MaterialDesc& md,
                                                   const GpuVertexLayout& layout) noexcept {
    GpuPipeline::Desc d;
    d.vert_path = md.vert_shader;
    d.frag_path = md.frag_shader;
    d.layout    = layout;

    d.raster.topology     = md.topology;
    d.raster.depth_test   = md.depth_test;
    d.raster.depth_write  = md.depth_write;
    d.raster.cull_back    = (md.cull == MatCull::Back);

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
