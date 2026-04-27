#include <monkey_dust/flare/power_loader.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace md::flare {

// ── chain helpers ─────────────────────────────────────────────────────────────

static FILE* ChainOpen(const char* const* chain, int n,
                       const char* rel_path, char* out_full, int out_sz) {
    for (int i = 0; i < n; ++i) {
        snprintf(out_full, (size_t)out_sz, "%s/%s", chain[i], rel_path);
        FILE* f = fopen(out_full, "rb");
        if (f) return f;
    }
    return nullptr;
}

// ── INCLUDE-aware parser ──────────────────────────────────────────────────────

using PowerLineCb = void(*)(const char* section, const char* key,
                             const char* value, void* ud);

static char s_pb[3][65536];

static void ParsePowerFile(const char* path, const char* const* chain, int n,
                           int depth, PowerLineCb cb, void* ud) {
    if (depth >= 3) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char* buf = s_pb[depth];
    size_t nr = fread(buf, 1, sizeof(s_pb[0]) - 1, f);
    fclose(f);
    buf[nr] = '\0';

    char cur_sec[64] = "";
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

        if (*lp && *lp != '#' && *lp != ';') {
            if (*lp == '[') {
                char* cl = strchr(lp + 1, ']');
                if (cl) {
                    int len = (int)(cl - (lp + 1));
                    if (len < (int)sizeof(cur_sec)) {
                        memcpy(cur_sec, lp + 1, (size_t)len);
                        cur_sec[len] = '\0';
                    }
                }
            } else if (strncmp(lp, "INCLUDE", 7) == 0 &&
                       (lp[7] == ' ' || lp[7] == '\t')) {
                char* ip = lp + 8;
                while (*ip == ' ' || *ip == '\t') ++ip;
                char full[512];
                FILE* fi = ChainOpen(chain, n, ip, full, sizeof(full));
                if (fi) {
                    fclose(fi);
                    ParsePowerFile(full, chain, n, depth + 1, cb, ud);
                }
            } else if (strncmp(lp, "APPEND", 6) == 0) {
                // mod merge directive — no runtime action
            } else {
                char* eq = strchr(lp, '=');
                if (eq) {
                    *eq = '\0';
                    char* ke = eq - 1;
                    while (ke >= lp && (*ke == ' ' || *ke == '\t')) --ke;
                    *(ke + 1) = '\0';
                    char* val = eq + 1;
                    while (*val == ' ' || *val == '\t') ++val;
                    cb(cur_sec, lp, val, ud);
                }
            }
        }
        p = next;
    }
}

// ── callback ──────────────────────────────────────────────────────────────────

static float ParseSec(const char* s) {
    char* end;
    float v = strtof(s, &end);
    while (*end == ' ') ++end;
    if (end[0] == 'm' && end[1] == 's') return v * 0.001f;
    return v;
}

struct PowerParseState {
    PowerRegistry* reg;
    PowerDef*      cur;
};

static void OnPowerLine(const char* section, const char* key,
                        const char* val, void* ud) {
    auto* s = static_cast<PowerParseState*>(ud);
    if (strcmp(section, "power") != 0) return;
    PowerRegistry& reg = *s->reg;

    if (strcmp(key, "id") == 0) {
        int id = (int)strtol(val, nullptr, 10);
        if (id <= 0 || id >= MAX_POWERS || reg.count >= MAX_POWERS) {
            s->cur = nullptr;
            return;
        }
        PowerDef* slot = nullptr;
        for (int i = 0; i < reg.count; ++i)
            if (reg.defs[i].id == id) { slot = &reg.defs[i]; break; }
        if (!slot) {
            slot = &reg.defs[reg.count++];
            memset(slot, 0, sizeof(*slot));
            slot->id = id;
        }
        s->cur = slot;
        return;
    }

    if (!s->cur) return;
    PowerDef& d = *s->cur;

    if      (strcmp(key, "name")        == 0) { strncpy(d.name, val, sizeof(d.name)-1); d.valid = true; }
    else if (strcmp(key, "type")        == 0) strncpy(d.type, val, sizeof(d.type)-1);
    else if (strcmp(key, "icon")        == 0) d.icon        = (int)strtol(val, nullptr, 10);
    else if (strcmp(key, "requires_mp") == 0) d.requires_mp = (int)strtol(val, nullptr, 10);
    else if (strcmp(key, "cooldown")    == 0) d.cooldown_s  = ParseSec(val);
    else if (strcmp(key, "radius")      == 0) d.radius      = strtof(val, nullptr);
    else if (strcmp(key, "lifespan")    == 0) d.lifespan_s  = ParseSec(val);
    else if (strcmp(key, "buff")        == 0) d.buff        = (val[0] == 't' || val[0] == '1');
    else if (strcmp(key, "use_hazard")  == 0) d.use_hazard  = (val[0] == 't' || val[0] == '1');
    else if (strcmp(key, "passive")     == 0) d.passive     = (val[0] == 't' || val[0] == '1');
}

// ── public API ────────────────────────────────────────────────────────────────

bool LoadPowers(const char* const* mod_chain, int n, PowerRegistry& out) {
    memset(&out, 0, sizeof(out));
    if (!mod_chain || n <= 0) return false;
    char path[512];
    snprintf(path, sizeof(path), "%s/powers/powers.txt", mod_chain[0]);

    bool ok = false;
    {
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); ok = true; }
    }
    if (ok) {
        PowerParseState st = {&out, nullptr};
        ParsePowerFile(path, mod_chain, n, 0, OnPowerLine, &st);
    }
    return ok;
}

const PowerDef* FindPower(const PowerRegistry& reg, int id) {
    for (int i = 0; i < reg.count; ++i)
        if (reg.defs[i].valid && reg.defs[i].id == id)
            return &reg.defs[i];
    return nullptr;
}

} // namespace md::flare
