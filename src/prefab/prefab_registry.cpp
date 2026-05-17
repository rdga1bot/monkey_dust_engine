#include <monkey_dust/prefab/prefab_registry.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>
#include <cstdlib>

// ── strstr-based JSON helpers (local) ────────────────────────────────────────

static bool pf_read_str(const char* p, const char* end,
                        const char* key, char* out, int out_sz) {
    const char* found = strstr(p, key);
    if (!found || found >= end) return false;
    found += strlen(key);
    const char* qs = strchr(found, '"');
    if (!qs || qs >= end) return false;
    const char* qe = strchr(qs + 1, '"');
    if (!qe || qe >= end) return false;
    int len = (int)(qe - qs - 1);
    if (len <= 0 || len >= out_sz) return false;
    memcpy(out, qs + 1, (size_t)len);
    out[len] = '\0';
    return true;
}

static float pf_read_float(const char* p, const char* end,
                            const char* key, float def) {
    const char* found = strstr(p, key);
    if (!found || found >= end) return def;
    found += strlen(key);
    while (found < end && *found != ':') ++found;
    if (found >= end) return def;
    ++found;
    while (found < end && (*found == ' ' || *found == '\t')) ++found;
    if (found >= end) return def;
    return (float)strtod(found, nullptr);
}

static int pf_read_int(const char* p, const char* end,
                        const char* key, int def) {
    const char* found = strstr(p, key);
    if (!found || found >= end) return def;
    found += strlen(key);
    while (found < end && *found != ':') ++found;
    if (found >= end) return def;
    ++found;
    while (found < end && (*found == ' ' || *found == '\t')) ++found;
    if (found >= end || (*found < '0' || *found > '9')) return def;
    return (int)strtol(found, nullptr, 10);
}

static bool pf_read_bool(const char* p, const char* end,
                          const char* key, bool def) {
    const char* found = strstr(p, key);
    if (!found || found >= end) return def;
    found += strlen(key);
    while (found < end && *found != ':') ++found;
    if (found >= end) return def;
    ++found;
    while (found < end && (*found == ' ' || *found == '\t')) ++found;
    if (found >= end) return def;
    if (strncmp(found, "true",  4) == 0) return true;
    if (strncmp(found, "false", 5) == 0) return false;
    return def;
}

// ── MdPrefabRegistry::LoadFromJson ───────────────────────────────────────────

int MdPrefabRegistry::LoadFromJson(const char* json) noexcept {
    if (!json) return -1;

    // Walk the top-level array: find each '{' ... '}' block.
    int loaded = 0;
    const char* p = json;
    while (*p) {
        const char* obj_s = strchr(p, '{');
        if (!obj_s) break;
        const char* obj_e = strchr(obj_s + 1, '}');
        if (!obj_e) break;

        MdPrefab pf{};
        // "name" is required
        if (!pf_read_str(obj_s, obj_e, "\"name\"", pf.name, MD_PREFAB_NAME_LEN)) {
            p = obj_e + 1;
            continue;
        }

        pf_read_str(obj_s, obj_e, "\"bt_template\"", pf.bt_template, MD_PREFAB_TMPL_LEN);
        if (!pf.bt_template[0]) {
            // Default to "wander" if not specified
            strncpy(pf.bt_template, "wander", MD_PREFAB_TMPL_LEN - 1);
        }

        pf.hp            = pf_read_float(obj_s, obj_e, "\"hp\"",            50.0f);
        pf.wander_radius = pf_read_float(obj_s, obj_e, "\"wander_radius\"", 3.0f);
        pf.is_aggressive = pf_read_bool (obj_s, obj_e, "\"is_aggressive\"", false);
        pf.alliance_group= (uint8_t)pf_read_int(obj_s, obj_e, "\"alliance_group\"", 0);

        // combat_profile: string "bandit"/"trader"/"none" or missing → bandit
        char cp[16] = {};
        if (pf_read_str(obj_s, obj_e, "\"combat_profile\"", cp, sizeof(cp))) {
            if      (strcmp(cp, "trader") == 0) pf.combat_profile = MD_COMBAT_TRADER;
            else if (strcmp(cp, "none")   == 0) pf.combat_profile = MD_COMBAT_NONE;
            else                                pf.combat_profile = MD_COMBAT_BANDIT;
        }

        if (Register(pf)) {
            ++loaded;
            MD_LOG(MD_LOG_INFO, "[MdPrefabRegistry] registered '%s' (bt=%s hp=%.0f)",
                   pf.name, pf.bt_template, pf.hp);
        } else {
            MD_LOG(MD_LOG_WARNING, "[MdPrefabRegistry] registry full, skipped '%s'", pf.name);
        }

        p = obj_e + 1;
    }

    return loaded;
}
