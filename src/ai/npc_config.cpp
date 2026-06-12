#include <monkey_dust/ai/npc_config.h>
#include <monkey_dust/platform/md_log.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// strstr-based JSON parser — no external JSON libs (project rule).
// 2-pass $inherit: Pass1 loads all raw entries; Pass2 resolves inheritance.

NpcConfigRegistry& NpcConfigRegistry::Get() {
    static NpcConfigRegistry inst;
    return inst;
}

const NpcConfig* NpcConfigRegistry::Find(const char* name) const {
    for (int i = 0; i < count_; ++i)
        if (strncmp(configs_[i].name, name, sizeof(configs_[0].name)) == 0)
            return &configs_[i];
    MD_LOG(MD_LOG_WARNING, "NpcConfigRegistry: '%s' not found", name);
    return nullptr;
}

// ── JSON field helpers ────────────────────────────────────────────────────────

static void read_str_field(const char* block, const char* key, char* out, int max) {
    const char* p = strstr(block, key);
    if (!p) return;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    if (*p == '"') ++p;
    int i = 0;
    while (*p && *p != '"' && i < max - 1) out[i++] = *p++;
    out[i] = '\0';
}

static int read_int_field(const char* block, const char* key) {
    const char* p = strstr(block, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    return atoi(p);
}

static float read_float_field(const char* block, const char* key) {
    const char* p = strstr(block, key);
    if (!p) return 0.f;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    return static_cast<float>(atof(p));
}

// Parse a single NpcConfig object from a null-terminated JSON block string.
// block should span from '{' to '}' of the object.
static void parse_npc_block(const char* block, NpcConfig& out) {
    memset(&out, 0, sizeof(out));
    read_str_field(block, "\"name\"",             out.name,             sizeof(out.name));
    read_str_field(block, "\"$inherit\"",          out.parent,           sizeof(out.parent));
    read_str_field(block, "\"cone_set\"",          out.cone_set,         sizeof(out.cone_set));
    read_str_field(block, "\"director_profile\"",  out.director_profile, sizeof(out.director_profile));
    read_str_field(block, "\"bt_day\"",            out.bt_day,           sizeof(out.bt_day));
    read_str_field(block, "\"bt_night\"",          out.bt_night,         sizeof(out.bt_night));
    out.max_health     = read_int_field  (block, "\"max_health\"");
    out.move_speed     = read_float_field(block, "\"move_speed\"");
    out.wander_radius  = read_float_field(block, "\"wander_radius\"");
}

// Apply non-zero/non-empty fields from child onto base (in-place).
static void apply_overrides(NpcConfig& base, const NpcConfig& child) {
    // Preserve base name; child's parent field not needed after resolution.
    if (child.cone_set[0])         strncpy(base.cone_set,         child.cone_set,         sizeof(base.cone_set) - 1);
    if (child.director_profile[0]) strncpy(base.director_profile, child.director_profile, sizeof(base.director_profile) - 1);
    if (child.bt_day[0])           strncpy(base.bt_day,           child.bt_day,           sizeof(base.bt_day) - 1);
    if (child.bt_night[0])         strncpy(base.bt_night,         child.bt_night,         sizeof(base.bt_night) - 1);
    if (child.max_health    != 0)  base.max_health    = child.max_health;
    if (child.move_speed    != 0.f)base.move_speed    = child.move_speed;
    if (child.wander_radius != 0.f)base.wander_radius = child.wander_radius;
}

// ── Load ──────────────────────────────────────────────────────────────────────

bool NpcConfigRegistry::Load(const char* json_path) {
    count_ = 0;

    FILE* f = fopen(json_path, "rb");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "NpcConfigRegistry: cannot open '%s'", json_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 65536) {
        fclose(f);
        MD_LOG(MD_LOG_WARNING, "NpcConfigRegistry: '%s' too large or empty", json_path);
        return false;
    }
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(sz) + 1));
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, static_cast<size_t>(sz), f);
    buf[sz] = '\0';
    fclose(f);

    // ── Pass 1: parse all objects ─────────────────────────────────────────────
    const char* p = buf;
    while (count_ < MAX_NPC_CONFIGS) {
        const char* name_key = strstr(p, "\"name\"");
        if (!name_key) break;

        // Find enclosing object boundaries
        const char* obj_start = name_key;
        while (obj_start > buf && *obj_start != '{') --obj_start;
        const char* obj_end = strstr(name_key, "}");
        if (!obj_end) obj_end = buf + sz;

        size_t block_len = static_cast<size_t>(obj_end - obj_start);
        if (block_len > 1023) block_len = 1023;
        char block[1024] = {};
        memcpy(block, obj_start, block_len);

        parse_npc_block(block, configs_[count_]);

        if (configs_[count_].name[0]) {
            ++count_;
        }
        p = obj_end + 1;
    }

    // ── Pass 2: resolve $inherit (one level) ─────────────────────────────────
    for (int i = 0; i < count_; ++i) {
        if (configs_[i].parent[0] == '\0') continue;

        // Find base (must already exist in configs_ — file order matters)
        const NpcConfig* base_ptr = nullptr;
        for (int j = 0; j < count_; ++j) {
            if (j == i) continue;
            if (strncmp(configs_[j].name, configs_[i].parent, sizeof(configs_[0].name)) == 0) {
                base_ptr = &configs_[j];
                break;
            }
        }
        if (!base_ptr) {
            MD_LOG(MD_LOG_WARNING, "NpcConfigRegistry: '%s' $inherit '%s' not found",
                   configs_[i].name, configs_[i].parent);
            continue;
        }

        // Merge: start from base, apply child overrides
        NpcConfig child_saved = configs_[i];
        configs_[i] = *base_ptr;                       // copy base
        strncpy(configs_[i].name, child_saved.name,    sizeof(configs_[i].name) - 1);
        configs_[i].parent[0] = '\0';                  // resolved — clear parent
        apply_overrides(configs_[i], child_saved);     // overlay child fields
    }

    free(buf);
    MD_LOG(MD_LOG_INFO, "NpcConfigRegistry: loaded %d configs from '%s'", count_, json_path);
    return count_ > 0;
}
