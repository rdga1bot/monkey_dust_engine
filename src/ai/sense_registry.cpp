#include <monkey_dust/ai/sense_registry.h>
#include <monkey_dust/platform/md_log.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
// Minimal strstr-based JSON parser — no external JSON libs (project rule).
// Parses data/ai/view_cone_sets.json.  Field order must match the spec.

static SenseRegistry s_instance;

SenseRegistry& SenseRegistry::Get() { return s_instance; }

const ViewConeSet* SenseRegistry::Find(const char* name) const {
    for (int i = 0; i < count_; ++i)
        if (strncmp(sets_[i].name, name, sizeof(sets_[0].name)) == 0)
            return &sets_[i];
    MD_LOG(MD_LOG_WARNING, "SenseRegistry: cone_set '%s' not found", name);
    return nullptr;
}

const ViewConeSet* SenseRegistry::At(uint8_t idx) const {
    if (idx >= count_) {
        MD_LOG(MD_LOG_WARNING, "SenseRegistry: idx %d out of range (%d sets)", (int)idx, (int)count_);
        return nullptr;
    }
    return &sets_[idx];
}

// ── JSON helpers ─────────────────────────────────────────────────────────────

static const char* skip_to(const char* p, char c) {
    while (*p && *p != c) ++p;
    return *p ? p + 1 : p;
}

static const char* read_str(const char* p, char* out, int max) {
    p = skip_to(p, '"');
    int i = 0;
    while (*p && *p != '"' && i < max - 1) out[i++] = *p++;
    out[i] = '\0';
    return *p ? p + 1 : p;
}

static float read_field_float(const char* block, const char* field) {
    const char* p = strstr(block, field);
    if (!p) return 0.f;
    p += strlen(field);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    return static_cast<float>(atof(p));
}

// ── Cone type name lookup ─────────────────────────────────────────────────────

static int cone_type_index(const char* t) {
    if (strstr(t, "Close"))      return 0;
    if (strstr(t, "Focused"))    return 1;
    if (strstr(t, "Normal"))     return 2;
    if (strstr(t, "Peripheral")) return 3;
    return -1;
}

// ── Load ──────────────────────────────────────────────────────────────────────

bool SenseRegistry::Load(const char* json_path) {
    FILE* f = fopen(json_path, "rb");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "SenseRegistry: cannot open '%s'", json_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 65536) {
        fclose(f);
        MD_LOG(MD_LOG_WARNING, "SenseRegistry: '%s' too large or empty", json_path);
        return false;
    }
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(sz) + 1));
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, static_cast<size_t>(sz), f);
    buf[sz] = '\0';
    fclose(f);

    count_ = 0;
    const char* p = buf;

    // Iterate over cone_set objects: find each "name" key in cone_sets array
    while (count_ < MAX_CONE_SETS) {
        const char* set_start = strstr(p, "\"name\"");
        if (!set_start) break;

        ViewConeSet& vs = sets_[count_];
        memset(&vs, 0, sizeof(vs));

        // Read set name
        read_str(set_start + 6, vs.name, sizeof(vs.name));

        // Find this set's "cones" array
        const char* cones_key = strstr(set_start, "\"cones\"");
        const char* next_set  = strstr(set_start + 6, "\"name\"");
        if (!cones_key || (next_set && cones_key > next_set)) {
            p = next_set ? next_set : set_start + 6;
            continue;
        }

        const char* cones_end = strstr(cones_key, "]");
        if (!cones_end) cones_end = buf + sz;

        // Parse individual cone objects between cones_key and cones_end
        const char* c = cones_key;
        vs.cone_count = 0;
        while (c < cones_end && vs.cone_count < MAX_VIEW_CONE_TYPES) {
            const char* type_key = strstr(c, "\"type\"");
            if (!type_key || type_key >= cones_end) break;

            char type_name[32] = {};
            const char* after = read_str(type_key + 6, type_name, sizeof(type_name));
            int ci = cone_type_index(type_name);
            if (ci < 0) { c = after; continue; }

            // Cone object spans from type_key to next '}'
            const char* cone_end = strstr(type_key, "}");
            if (!cone_end || cone_end >= cones_end) cone_end = cones_end;

            // Extract fields from this cone block (null-terminate a local copy)
            size_t block_len = static_cast<size_t>(cone_end - type_key);
            if (block_len > 511) block_len = 511;
            char block[512] = {};
            memcpy(block, type_key, block_len);

            ViewCone& vc    = vs.cones[ci];
            vc.h_angle_deg  = read_field_float(block, "\"h_angle\"");
            vc.v_angle_deg  = read_field_float(block, "\"v_angle\"");
            vc.length_m     = read_field_float(block, "\"length\"");
            vc.dist_lo      = read_field_float(block, "\"dist_lo\"");
            vc.dist_hi      = read_field_float(block, "\"dist_hi\"");
            vc.exposure_lo  = read_field_float(block, "\"exposure_lo\"");
            vc.exposure_hi  = read_field_float(block, "\"exposure_hi\"");
            vc.movement_lo  = read_field_float(block, "\"movement_lo\"");
            vc.movement_hi  = read_field_float(block, "\"movement_hi\"");
            vc.stance_lo    = read_field_float(block, "\"stance_lo\"");
            vc.stance_hi    = read_field_float(block, "\"stance_hi\"");

            vs.cone_count++;
            c = cone_end + 1;
        }

        vs.threshold_lo = read_field_float(set_start, "\"activation_threshold_lo\"");
        vs.threshold_hi = read_field_float(set_start, "\"activation_threshold_hi\"");

        p = next_set ? next_set : set_start + 6;
        ++count_;
    }

    free(buf);
    MD_LOG(MD_LOG_INFO, "SenseRegistry: loaded %d cone sets from '%s'", (int)count_, json_path);
    return count_ > 0;
}
