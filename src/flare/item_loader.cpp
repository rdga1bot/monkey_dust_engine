#include <monkey_dust/flare/item_loader.h>
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

using ItemLineCb = void(*)(const char* section, const char* key,
                           const char* value, void* ud);

static char s_ib[3][65536];

static void ParseItemFile(const char* path, const char* const* chain, int n,
                          int depth, ItemLineCb cb, void* ud) {
    if (depth >= 3) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char* buf = s_ib[depth];
    size_t nr = fread(buf, 1, sizeof(s_ib[0]) - 1, f);
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
                    ParseItemFile(full, chain, n, depth + 1, cb, ud);
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

struct ItemParseState {
    ItemRegistry* reg;
    ItemDef*      cur;
};

static FlareItemType GuessType(const ItemDef& d) {
    if (d.dmg_melee_min > 0 || d.dmg_melee_max > 0)   return FlareItemType::MELEE;
    if (d.dmg_ranged_min > 0 || d.dmg_ranged_max > 0) return FlareItemType::RANGED;
    if (d.dmg_ment_min > 0 || d.dmg_ment_max > 0)     return FlareItemType::MELEE;
    if (d.abs_min > 0 || d.abs_max > 0)                return FlareItemType::ARMOR;
    if (d.max_quantity > 1)                            return FlareItemType::POTION;
    return FlareItemType::OTHER;
}

static void OnItemLine(const char* section, const char* key,
                       const char* val, void* ud) {
    auto* s = static_cast<ItemParseState*>(ud);
    if (strcmp(section, "item") != 0) return;
    ItemRegistry& reg = *s->reg;

    if (strcmp(key, "id") == 0) {
        int id = (int)strtol(val, nullptr, 10);
        if (id <= 0 || id >= MAX_ITEMS || reg.count >= MAX_ITEMS) {
            s->cur = nullptr;
            return;
        }
        ItemDef* slot = nullptr;
        for (int i = 0; i < reg.count; ++i)
            if (reg.defs[i].id == id) { slot = &reg.defs[i]; break; }
        if (!slot) {
            slot = &reg.defs[reg.count++];
            memset(slot, 0, sizeof(*slot));
            slot->id = id;
            slot->max_quantity = 1;
        }
        s->cur = slot;
        return;
    }

    if (!s->cur) return;
    ItemDef& d = *s->cur;

    if      (strcmp(key, "name")         == 0) { strncpy(d.name, val, sizeof(d.name)-1); d.valid = true; }
    else if (strcmp(key, "icon")         == 0) d.icon         = (int)strtol(val, nullptr, 10);
    else if (strcmp(key, "price")        == 0) d.price        = (int)strtol(val, nullptr, 10);
    else if (strcmp(key, "price_sell")   == 0) d.price_sell   = (int)strtol(val, nullptr, 10);
    else if (strcmp(key, "max_quantity") == 0) d.max_quantity = (int)strtol(val, nullptr, 10);
    else if (strcmp(key, "quality")      == 0) {
        if (strcmp(val, "currency") == 0) d.type = FlareItemType::CURRENCY;
    }
    else if (strcmp(key, "abs") == 0) {
        const char* c = strchr(val, ',');
        d.abs_min = (int)strtol(val, nullptr, 10);
        d.abs_max = c ? (int)strtol(c + 1, nullptr, 10) : d.abs_min;
    }
    else if (strcmp(key, "dmg_melee") == 0) {
        const char* c = strchr(val, ',');
        d.dmg_melee_min = (int)strtol(val, nullptr, 10);
        d.dmg_melee_max = c ? (int)strtol(c + 1, nullptr, 10) : d.dmg_melee_min;
    }
    else if (strcmp(key, "dmg_ment") == 0) {
        const char* c = strchr(val, ',');
        d.dmg_ment_min = (int)strtol(val, nullptr, 10);
        d.dmg_ment_max = c ? (int)strtol(c + 1, nullptr, 10) : d.dmg_ment_min;
    }
    else if (strcmp(key, "dmg_ranged") == 0) {
        const char* c = strchr(val, ',');
        d.dmg_ranged_min = (int)strtol(val, nullptr, 10);
        d.dmg_ranged_max = c ? (int)strtol(c + 1, nullptr, 10) : d.dmg_ranged_min;
    }
}

// ── public API ────────────────────────────────────────────────────────────────

bool LoadItems(const char* const* mod_chain, int n, ItemRegistry& out) {
    memset(&out, 0, sizeof(out));
    if (!mod_chain || n <= 0) return false;
    char path[512];
    snprintf(path, sizeof(path), "%s/items/items.txt", mod_chain[0]);

    ItemParseState st = {&out, nullptr};
    bool ok = false;
    {
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); ok = true; }
    }
    if (ok) ParseItemFile(path, mod_chain, n, 0, OnItemLine, &st);

    for (int i = 0; i < out.count; ++i) {
        ItemDef& d = out.defs[i];
        if (d.valid && d.type == FlareItemType::NONE)
            d.type = GuessType(d);
    }
    return ok;
}

const ItemDef* FindItem(const ItemRegistry& reg, int id) {
    for (int i = 0; i < reg.count; ++i)
        if (reg.defs[i].valid && reg.defs[i].id == id)
            return &reg.defs[i];
    return nullptr;
}

} // namespace md::flare
