#include <monkey_dust/flare/faction_loader.h>
#include <monkey_dust/flare/enemy_loader.h>
#include <cstring>

namespace md::flare {

// ── helpers ───────────────────────────────────────────────────────────────────

// Extract the primary category (first comma-separated token) from a category string.
// Returns length written (0 if empty).
static int PrimaryCategory(const char* cats, char* out, int out_sz) {
    const char* end = strchr(cats, ',');
    int len = end ? (int)(end - cats) : (int)strlen(cats);
    if (len <= 0 || len >= out_sz) return 0;
    memcpy(out, cats, (size_t)len);
    out[len] = '\0';
    // trim trailing whitespace
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t')) {
        out[--len] = '\0';
    }
    return len;
}

// ── public API ────────────────────────────────────────────────────────────────

void BuildFactionsFromEnemies(const EnemyRegistry&  enemies,
                              FlareFactionRegistry& out) {
    memset(&out, 0, sizeof(out));
    char primary[48];
    for (int i = 0; i < enemies.count; ++i) {
        const EnemyDef& e = enemies.defs[i];
        if (!e.valid || e.categories[0] == '\0') continue;
        if (PrimaryCategory(e.categories, primary, sizeof(primary)) > 0)
            GetOrAddFaction(out, primary);
    }
}

uint8_t GetOrAddFaction(FlareFactionRegistry& reg, const char* name) {
    if (!name || name[0] == '\0') return 0;
    // check existing
    for (int i = 0; i < reg.count; ++i) {
        if (reg.entries[i].valid && strcmp(reg.entries[i].name, name) == 0)
            return reg.entries[i].id;
    }
    if (reg.count >= MAX_FLARE_FACTIONS) return 0;
    FlareFactionEntry& e = reg.entries[reg.count++];
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = '\0';
    e.id    = (uint8_t)reg.count;   // 1-based
    e.valid = true;
    return e.id;
}

uint8_t FindFactionId(const FlareFactionRegistry& reg, const char* name) {
    for (int i = 0; i < reg.count; ++i)
        if (reg.entries[i].valid && strcmp(reg.entries[i].name, name) == 0)
            return reg.entries[i].id;
    return 0;
}

} // namespace md::flare
