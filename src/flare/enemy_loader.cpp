#include <monkey_dust/flare/enemy_loader.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <monkey_dust/compat/md_dirent.h>

namespace md::flare {

// ── helpers ───────────────────────────────────────────────────────────────────

static int ParseMs(const char* s) {
    char* end;
    int v = (int)strtol(s, &end, 10);
    while (*end == ' ') ++end;
    if (end[0] == 'm' && end[1] == 's') return v;
    if (end[0] == 's') return v * 1000;
    return v;
}

// Resolve <rel_path> against each mod in chain; open the first that exists.
// Returns nullptr if none found.
static FILE* ChainOpen(const char* const* chain, int n,
                       const char* rel_path, char* out_full, int out_sz) {
    for (int i = 0; i < n; ++i) {
        snprintf(out_full, (size_t)out_sz, "%s/%s", chain[i], rel_path);
        FILE* f = fopen(out_full, "rb");
        if (f) return f;
    }
    return nullptr;
}

// Parse "stat_name,integer" → both parts.
static bool SplitStat(const char* val, char* out_name, int name_sz, int* out_v) {
    const char* comma = strchr(val, ',');
    if (!comma) return false;
    int len = (int)(comma - val);
    if (len <= 0 || len >= name_sz) return false;
    memcpy(out_name, val, (size_t)len);
    out_name[len] = '\0';
    *out_v = (int)strtol(comma + 1, nullptr, 10);
    return true;
}

// ── ParseEnemyFile ────────────────────────────────────────────────────────────
// INCLUDE paths are resolved via ChainOpen across the full mod chain.

static char s_lb[3][65536];

static void ParseEnemyFile(const char* path, const char* const* chain, int n,
                           EnemyDef& def, int depth) {
    if (depth >= 3) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char* buf = s_lb[depth];
    size_t nr = fread(buf, 1, sizeof(s_lb[0]) - 1, f);
    fclose(f);
    buf[nr] = '\0';

    char* p = buf;
    while (*p) {
        char* eol = p;
        while (*eol && *eol != '\n' && *eol != '\r') ++eol;
        char* next = eol;
        if (*next == '\r') ++next;
        if (*next == '\n') ++next;
        *eol = '\0';

        char* lp = p;
        while (*lp == ' ' || *lp == '\t') ++lp;

        if (*lp && *lp != '#' && *lp != ';' && *lp != '[') {
            if (strncmp(lp, "INCLUDE", 7) == 0 &&
                (lp[7] == ' ' || lp[7] == '\t')) {
                char* ip = lp + 8;
                while (*ip == ' ' || *ip == '\t') ++ip;
                char full[512];
                FILE* fi = ChainOpen(chain, n, ip, full, sizeof(full));
                if (fi) {
                    fclose(fi);
                    ParseEnemyFile(full, chain, n, def, depth + 1);
                }
            } else {
                char* eq = strchr(lp, '=');
                if (eq) {
                    *eq = '\0';
                    char* ke = eq - 1;
                    while (ke >= lp && (*ke == ' ' || *ke == '\t')) --ke;
                    *(ke + 1) = '\0';
                    char* val = eq + 1;
                    while (*val == ' ' || *val == '\t') ++val;

                    if      (strcmp(lp, "name")        == 0) { strncpy(def.name, val, sizeof(def.name)-1); def.valid = true; }
                    else if (strcmp(lp, "animations")  == 0) strncpy(def.animations, val, sizeof(def.animations)-1);
                    else if (strcmp(lp, "categories")  == 0) strncpy(def.categories, val, sizeof(def.categories)-1);
                    else if (strcmp(lp, "level")       == 0) def.level         = (int)strtol(val, nullptr, 10);
                    else if (strcmp(lp, "xp")          == 0) def.xp            = (int)strtol(val, nullptr, 10);
                    else if (strcmp(lp, "speed")       == 0) def.speed         = strtof(val, nullptr);
                    else if (strcmp(lp, "humanoid")    == 0) def.humanoid      = (val[0] == 't' || val[0] == '1');
                    else if (strcmp(lp, "melee_range") == 0) def.melee_range   = strtof(val, nullptr);
                    else if (strcmp(lp, "threat_range")== 0) def.threat_range  = strtof(val, nullptr);
                    else if (strcmp(lp, "turn_delay")  == 0) def.turn_delay_ms = ParseMs(val);
                    else if (strcmp(lp, "stat")        == 0) {
                        char sname[32]; int sval;
                        if (SplitStat(val, sname, sizeof(sname), &sval)) {
                            if      (strcmp(sname, "hp")            == 0) def.hp             = sval;
                            else if (strcmp(sname, "accuracy")      == 0) def.accuracy       = sval;
                            else if (strcmp(sname, "avoidance")     == 0) def.avoidance      = sval;
                            else if (strcmp(sname, "absorb_min")    == 0) def.absorb_min     = sval;
                            else if (strcmp(sname, "absorb_max")    == 0) def.absorb_max     = sval;
                            else if (strcmp(sname, "dmg_melee_min") == 0) def.dmg_melee_min  = sval;
                            else if (strcmp(sname, "dmg_melee_max") == 0) def.dmg_melee_max  = sval;
                            else if (strcmp(sname, "dmg_ment_min")  == 0) def.dmg_ment_min   = sval;
                            else if (strcmp(sname, "dmg_ment_max")  == 0) def.dmg_ment_max   = sval;
                            else if (strcmp(sname, "dmg_ranged_min")== 0) def.dmg_ranged_min = sval;
                            else if (strcmp(sname, "dmg_ranged_max")== 0) def.dmg_ranged_max = sval;
                        }
                    }
                }
            }
        }
        p = next;
    }
}

// ── ScanDir ───────────────────────────────────────────────────────────────────

static void ScanDir(const char* dir_path, const char* const* chain, int n,
                    EnemyRegistry& out) {
    DIR* d = opendir(dir_path);
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);

        if (ent->d_type == DT_DIR) {
            ScanDir(full, chain, n, out);
        } else if (ent->d_type == DT_REG) {
            const char* ext = strrchr(ent->d_name, '.');
            if (!ext || strcmp(ext, ".txt") != 0) continue;
            if (out.count >= MAX_ENEMY_TYPES) break;

            EnemyDef& def = out.defs[out.count];
            memset(&def, 0, sizeof(def));

            int stem_len = (int)(ext - ent->d_name);
            if (stem_len > (int)sizeof(def.id_str) - 1)
                stem_len = (int)sizeof(def.id_str) - 1;
            memcpy(def.id_str, ent->d_name, (size_t)stem_len);
            def.id_str[stem_len] = '\0';

            ParseEnemyFile(full, chain, n, def, 0);
            def.valid = true;
            ++out.count;
        }
    }
    closedir(d);
}

// ── public API ────────────────────────────────────────────────────────────────

bool LoadEnemies(const char* const* mod_chain, int n, EnemyRegistry& out) {
    memset(&out, 0, sizeof(out));
    if (!mod_chain || n <= 0) return false;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/enemies", mod_chain[0]);
    DIR* test = opendir(dir);
    if (!test) return false;
    closedir(test);
    ScanDir(dir, mod_chain, n, out);
    return true;
}

const EnemyDef* FindEnemy(const EnemyRegistry& reg, const char* id_str) {
    for (int i = 0; i < reg.count; ++i)
        if (reg.defs[i].valid && strcmp(reg.defs[i].id_str, id_str) == 0)
            return &reg.defs[i];
    return nullptr;
}

} // namespace md::flare
