#include <monkey_dust/world/faction_system.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static bool ParseInt(const char* p, const char* key, int& out) {
    const char* found = strstr(p, key);
    if (!found) return false;
    found += strlen(key);
    while (*found && (*found == '"' || *found == ':' || *found == ' '))
        found++;
    if (!*found) return false;
    out = (int)strtol(found, nullptr, 10);
    return true;
}

static bool ParseStr(const char* p, const char* key, char* out, int max_len) {
    const char* found = strstr(p, key);
    if (!found) return false;
    found += strlen(key);
    const char* q = strchr(found, '"');
    if (!q) return false;
    q++;
    int i = 0;
    while (*q && *q != '"' && i < max_len - 1)
        out[i++] = *q++;
    out[i] = '\0';
    return true;
}

int FactionSystem::LoadFromFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[FactionSystem] Cannot open: %s\n", path); return 0; }
    static char buf[8192];
    int bytes = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[bytes] = '\0';
    faction_count_ = 0;
    const char* cursor = buf;
    while (faction_count_ < MAX_FACTIONS) {
        const char* id_pos = strstr(cursor, "\"id\"");
        if (!id_pos) break;
        FactionData& fd = factions_[faction_count_];
        memset(&fd, 0, sizeof(fd));
        int id_val = 0;
        if (!ParseInt(id_pos, "\"id\"", id_val)) { cursor = id_pos + 4; continue; }
        fd.id = (uint32_t)id_val;
        int def_rel = 0;
        ParseInt(id_pos, "\"default_relation\"", def_rel);
        fd.default_relation = (int8_t)def_rel;
        for (int i = 0; i <= MAX_FACTIONS; ++i) fd.relations[i] = fd.default_relation;
        fd.relations[fd.id] = 100;
        ParseStr(id_pos, "\"name\"", fd.name, sizeof(fd.name));
        const char* rel_arr  = strstr(id_pos, "\"relations\"");
        const char* next_id  = strstr(id_pos + 4, "\"id\"");
        if (rel_arr && (!next_id || rel_arr < next_id)) {
            const char* rel_cursor = rel_arr;
            while (true) {
                const char* to_pos = strstr(rel_cursor, "\"to\"");
                if (!to_pos) break;
                if (next_id && to_pos >= next_id) break;
                int to_val = 0, val_val = 0;
                if (!ParseInt(to_pos, "\"to\"", to_val)) { rel_cursor = to_pos + 4; continue; }
                const char* val_pos = strstr(to_pos, "\"value\"");
                if (!val_pos || (next_id && val_pos >= next_id)) { rel_cursor = to_pos + 4; continue; }
                ParseInt(val_pos, "\"value\"", val_val);
                if (to_val >= 0 && to_val <= MAX_FACTIONS)
                    fd.relations[to_val] = (int8_t)val_val;
                rel_cursor = val_pos + 7;
            }
        }
        fd.loaded = true;
        faction_count_++;
        cursor = id_pos + 4;
    }
    fprintf(stdout, "[FactionSystem] Loaded %d factions from %s\n", faction_count_, path);
    return faction_count_;
}

static bool ParseFloat(const char* p, const char* key, float& out) {
    const char* found = strstr(p, key);
    if (!found) return false;
    found += strlen(key);
    while (*found && (*found == '"' || *found == ':' || *found == ' '))
        found++;
    if (!*found) return false;
    out = (float)strtod(found, nullptr);
    return true;
}

int FactionSystem::LoadFromJson(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "[FactionSystem] Cannot open: %s\n", path); return 0; }
    static char buf[32768];
    int bytes = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[bytes] = '\0';

    const char* arr = strstr(buf, "\"factions\"");
    if (!arr) return 0;
    arr = strchr(arr, '[');
    if (!arr) return 0;

    faction_count_ = 0;
    const char* cursor = arr + 1;
    uint32_t nid = 0;

    while (faction_count_ < MAX_FACTIONS) {
        const char* obj = strchr(cursor, '{');
        if (!obj) break;
        const char* end = strchr(obj + 1, '}');
        if (!end) break;

        FactionData& fd = factions_[faction_count_];
        memset(&fd, 0, sizeof(fd));
        fd.id = nid;
        fd.color_r = 0.7f; fd.color_g = 0.7f; fd.color_b = 0.7f;

        ParseStr(obj, "\"id\"",   fd.slug, sizeof(fd.slug));
        ParseStr(obj, "\"name\"", fd.name, sizeof(fd.name));

        char attitude[64] = {};
        ParseStr(obj, "\"attitude\"", attitude, sizeof(attitude));
        if      (strstr(attitude, "hostile"))    fd.default_relation = -50;
        else if (strstr(attitude, "aggressive")) fd.default_relation = -20;
        else if (strstr(attitude, "friendly"))   fd.default_relation =  50;
        else                                     fd.default_relation =   0;

        const char* col = strstr(obj, "\"color_rgb\"");
        if (col && col < end) {
            const char* cb = strchr(col, '[');
            if (cb) {
                float r = 0.7f, g = 0.7f, b = 0.7f;
                sscanf(cb + 1, "%f , %f , %f", &r, &g, &b);
                fd.color_r = r; fd.color_g = g; fd.color_b = b;
            }
        }

        for (int i = 0; i <= MAX_FACTIONS; ++i) fd.relations[i] = fd.default_relation;
        fd.relations[fd.id] = 100;
        fd.loaded = true;
        faction_count_++;
        nid++;
        cursor = end + 1;
    }
    fprintf(stdout, "[FactionSystem] Loaded %d Kenshi factions from %s\n", faction_count_, path);
    return faction_count_;
}

FactionData* FactionSystem::FindFaction(uint32_t id) {
    for (int i = 0; i < faction_count_; ++i)
        if (factions_[i].id == id) return &factions_[i];
    return nullptr;
}
const FactionData* FactionSystem::FindFaction(uint32_t id) const {
    for (int i = 0; i < faction_count_; ++i)
        if (factions_[i].id == id) return &factions_[i];
    return nullptr;
}
int8_t FactionSystem::GetRelation(uint32_t from, uint32_t to) const {
    if (from == to) return 100;
    const FactionData* fd = FindFaction(from);
    if (!fd) return 0;
    if (to <= MAX_FACTIONS) return fd->relations[to];
    return fd->default_relation;
}
const char* FactionSystem::GetName(uint32_t id) const {
    const FactionData* fd = FindFaction(id);
    if (!fd || !fd->loaded) return "unknown";
    return fd->name;
}
void FactionSystem::SetRelation(uint32_t from, uint32_t to, int8_t value) {
    FactionData* fd = FindFaction(from);
    if (fd && to <= (uint32_t)MAX_FACTIONS) fd->relations[to] = value;
}
void FactionSystem::ModRelation(uint32_t from, uint32_t to, int8_t delta) {
    FactionData* fd = FindFaction(from);
    if (!fd || to > (uint32_t)MAX_FACTIONS) return;
    int new_val = fd->relations[to] + delta;
    if (new_val >  100) new_val =  100;
    if (new_val < -100) new_val = -100;
    fd->relations[to] = (int8_t)new_val;
}

// ── FCS import (Phase 4) ──────────────────────────────────────────────────────
// Parses GENERATION_TEMPLATE_FACTION records from gamedata.base / *.mod.
// Tool-only path — kenshi_data_parser.h is in tools/; we use a lightweight
// strstr-scan here instead to keep engine/ clean of tool headers.
//
// Binary record field names confirmed from hex analysis:
//   "name" (string), "faction" (string ref), "relations" child records
//
// Simple extraction: scan raw bytes for faction name fields.
int FactionSystem::LoadFromFcs(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    rewind(f);
    if (fsz <= 0 || fsz > 32 * 1024 * 1024) { fclose(f); return 0; }

    char* buf = new char[(size_t)fsz + 1];
    size_t rd = fread(buf, 1, (size_t)fsz, f);
    fclose(f);
    buf[rd] = 0;

    int added = 0;
    // Scan for "GENERATION_TEMPLATE_FACTION" type records.
    // After the type+ref strings we extract the "name" string field.
    // We use a heuristic: find ASCII "name" keys preceded by faction type markers.
    const char* p = buf;
    const char* end = buf + rd;
    while (p < end - 64 && added < MAX_FACTIONS) {
        // Look for the faction type marker in the raw bytes.
        const char* marker = (const char*)memchr(p, 'G', (size_t)(end - p));
        if (!marker) break;
        if (strncmp(marker, "GENERATION_TEMPLATE_FACTION", 27) != 0) {
            p = marker + 1;
            continue;
        }
        // Found a faction record: scan forward for a "name" field string.
        // In FCS format the string value follows [uint32 key_len="name"][uint32 str_len][str].
        // We just search for the literal "name\0" bytes in the next 256 bytes.
        const char* scan_end = marker + 256 < end ? marker + 256 : end;
        const char* name_key = nullptr;
        for (const char* s = marker; s < scan_end - 4; ++s) {
            if (s[0]=='n' && s[1]=='a' && s[2]=='m' && s[3]=='e' && s[4]=='\0') {
                name_key = s;
                break;
            }
        }
        if (name_key && faction_count_ < MAX_FACTIONS) {
            // After "name\0": [uint32 str_len][char str[str_len]]
            const uint8_t* after = (const uint8_t*)(name_key + 5);
            if (after + 4 < (const uint8_t*)end) {
                uint32_t slen = (uint32_t)after[0]
                              | ((uint32_t)after[1] << 8)
                              | ((uint32_t)after[2] << 16)
                              | ((uint32_t)after[3] << 24);
                if (slen > 0 && slen < 64 && after + 4 + slen <= (const uint8_t*)end) {
                    FactionData& fd = factions_[faction_count_];
                    memset(&fd, 0, sizeof(fd));
                    uint32_t copy = slen < 31u ? slen : 31u;
                    memcpy(fd.name, after + 4, copy);
                    fd.name[copy] = 0;
                    fd.id     = (uint32_t)faction_count_;
                    fd.loaded = true;
                    faction_count_++;
                    added++;
                }
            }
        }
        p = marker + 27;
    }

    delete[] buf;
    return added;
}
