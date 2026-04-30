#include <monkey_dust/flare/tile_map.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

namespace md::flare {

// ── TileMetaRegistry ──────────────────────────────────────────────────────────

void TileMetaRegistry::Clear() { count = 0; }

const TileMeta* TileMetaRegistry::Find(uint16_t tile_id) const {
    if (tile_id == 0) return nullptr;
    for (int i = 0; i < count; ++i)
        if (entries[i].tile_id == tile_id) return &entries[i];
    return nullptr;
}

bool TileMetaRegistry::Add(const TileMeta& m) {
    if (count >= MAX_TILE_META) {
        fprintf(stderr, "[TileMeta] registry full (MAX=%d), tile_id=%d dropped\n",
                MAX_TILE_META, (int)m.tile_id);
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (entries[i].tile_id == m.tile_id) { entries[i] = m; return true; }
    }
    entries[count++] = m;
    return true;
}

void TileMetaRegistry::DumpFirst(int n) const {
    int show = (n < count) ? n : count;
    fprintf(stdout, "[TileMeta] registry contains %d entries (showing first %d):\n",
            count, show);
    for (int i = 0; i < show; ++i) {
        const TileMeta& m = entries[i];
        fprintf(stdout, "  [%d] id=%u src=(%d,%d) size=(%d,%d) offset=(%d,%d)\n",
                i, (unsigned)m.tile_id,
                (int)m.src_x, (int)m.src_y,
                (int)m.w, (int)m.h,
                (int)m.offset_x, (int)m.offset_y);
    }
}

// ── Tilesetdef parser ─────────────────────────────────────────────────────────

static bool ParseTilesetDefFile(const char* path, TileMetaRegistry& meta) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "tile=", 5) != 0) continue;
        const char* p = line + 5;
        int id, sx, sy, w, h, ox, oy;
        if (sscanf(p, "%d,%d,%d,%d,%d,%d,%d", &id, &sx, &sy, &w, &h, &ox, &oy) == 7) {
            TileMeta m;
            m.tile_id  = (uint16_t)id;
            m.src_x    = (int16_t)sx;
            m.src_y    = (int16_t)sy;
            m.w        = (int16_t)w;
            m.h        = (int16_t)h;
            m.offset_x = (int16_t)ox;
            m.offset_y = (int16_t)oy;
            meta.Add(m);
        } else {
            fprintf(stderr, "[TileMeta] bad format: %s", line);
        }
    }
    fclose(f);
    return meta.count > 0;
}

// Extract parent directory in-place (removes last path component).
static void DirOf(const char* path, char* out, int n) {
    snprintf(out, (size_t)n, "%s", path);
    char* last = nullptr;
    for (char* c = out; *c; ++c) if (*c == '/') last = c;
    if (last) *last = '\0';
    else { out[0] = '.'; out[1] = '\0'; }
}

// Scan siblings of the map's mod directory for a tilesetdef file.
// Map is at: {mods_root}/{mod}/{maps}/{file}.txt
// tileset_def is relative to a mod root, e.g. "tilesetdefs/tileset_grassland.txt".
static bool TryLoadTilesetDef(const char* map_path, const char* tileset_def,
                               TileMetaRegistry& meta) {
    if (!tileset_def || !tileset_def[0]) return false;

    char map_dir[512], mod_dir[512], mods_root[512];
    DirOf(map_path, map_dir, sizeof(map_dir));   // strip filename → .../maps
    DirOf(map_dir, mod_dir, sizeof(mod_dir));    // strip maps/  → .../mod
    DirOf(mod_dir, mods_root, sizeof(mods_root)); // strip mod/  → .../mods

    DIR* d = opendir(mods_root);
    if (!d) {
        // Fallback: try same mod dir
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s/%s", mod_dir, tileset_def);
        return ParseTilesetDefFile(candidate, meta);
    }

    struct dirent* ent;
    bool found = false;
    while (!found && (ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s/%s/%s",
                 mods_root, ent->d_name, tileset_def);
        struct stat st;
        if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode)) {
            fprintf(stdout, "[TileMeta] loading tilesetdef: %s\n", candidate);
            found = ParseTilesetDefFile(candidate, meta);
        }
    }
    closedir(d);
    return found;
}

// ── Shared helpers ────────────────────────────────────────────────────────────

static void CpStr(char* dst, int n, const char* src) {
    int i = 0;
    for (; i < n - 1 && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static LayerType LayerTypeOf(const char* s) {
    if (strcmp(s, "background") == 0) return LayerType::BACKGROUND;
    if (strcmp(s, "fringe")     == 0) return LayerType::FRINGE;
    if (strcmp(s, "object")     == 0) return LayerType::OBJECT;
    if (strcmp(s, "collision")  == 0) return LayerType::COLLISION;
    return LayerType::UNKNOWN;
}

// Parse one CSV line of tile IDs into layer.tiles[row * MAX_MAP_WIDTH + col].
static void ParseTileRow(const char* p, TileMapLayer& layer, int row, int map_w) {
    if (row < 0 || row >= MAX_MAP_HEIGHT) return;
    int col = 0;
    while (*p && col < map_w && col < MAX_MAP_WIDTH) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '\n' || *p == '\r') break;
        long v = strtol(p, (char**)&p, 10);
        layer.tiles[row * MAX_MAP_WIDTH + col] = (uint16_t)(v > 0 ? v : 0);
        ++col;
        if (*p == ',') ++p;
    }
}

// ── Flare .txt loader ─────────────────────────────────────────────────────────

static bool LoadTxt(const char* path, FlareMap& m) {
    FILE* f = fopen(path, "r");
    if (!f) return false;

    memset(&m, 0, sizeof(m));
    m.tile_w = 192; m.tile_h = 96;

    enum { S_NONE, S_HEADER, S_TILESETS, S_LAYER, S_ENEMY, S_SKIP } state = S_NONE;

    TileMapLayer* cur_layer  = nullptr;
    bool          reading_data = false;
    int           tile_row     = 0;

    FlareSpawn cur_enemy;
    bool       enemy_open = false;
    memset(&cur_enemy, 0, sizeof(cur_enemy));

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

        if (len == 0) { reading_data = false; continue; }

        if (line[0] == '[') {
            char sec[32] = {};
            for (int i = 1; i < len && line[i] != ']' && i < 31; ++i) sec[i-1] = line[i];
            reading_data = false;

            // Commit pending enemy block before switching section.
            if (enemy_open && cur_enemy.category[0] && m.spawn_count < MAX_SPAWNS_PER_MAP) {
                m.spawns[m.spawn_count++] = cur_enemy;
            }
            enemy_open = false;

            if      (strcmp(sec, "header")   == 0) { state = S_HEADER; }
            else if (strcmp(sec, "tilesets") == 0) { state = S_TILESETS; }
            else if (strcmp(sec, "layer")    == 0) {
                state = S_LAYER;
                if (m.layer_count < MAX_MAP_LAYERS) {
                    cur_layer = &m.layers[m.layer_count++];
                    memset(cur_layer, 0, sizeof(*cur_layer));
                    cur_layer->type = LayerType::BACKGROUND;
                    tile_row = 0;
                } else {
                    cur_layer = nullptr;
                }
            }
            else if (strcmp(sec, "enemy") == 0) {
                state = S_ENEMY;
                memset(&cur_enemy, 0, sizeof(cur_enemy));
                cur_enemy.number_min = 1;
                enemy_open = true;
            }
            else { state = S_SKIP; cur_layer = nullptr; }
            continue;
        }

        if (state == S_SKIP) continue;

        if (reading_data && cur_layer) {
            ParseTileRow(line, *cur_layer, tile_row, m.width);
            ++tile_row;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;
        while (*val == ' ' || *val == '\t') ++val;

        if (state == S_HEADER) {
            if      (strcmp(key, "width")      == 0) m.width  = atoi(val);
            else if (strcmp(key, "height")     == 0) m.height = atoi(val);
            else if (strcmp(key, "tilewidth")  == 0) m.tile_w = atoi(val);
            else if (strcmp(key, "tileheight") == 0) m.tile_h = atoi(val);
            else if (strcmp(key, "music")      == 0) CpStr(m.music_path,  sizeof(m.music_path),  val);
            else if (strcmp(key, "tileset")    == 0) CpStr(m.tileset_def, sizeof(m.tileset_def), val);
            else if (strcmp(key, "title")      == 0) CpStr(m.title,       sizeof(m.title),       val);
            else if (strcmp(key, "hero_pos")   == 0) sscanf(val, "%f,%f", &m.hero_x, &m.hero_y);
        }
        else if (state == S_TILESETS && strcmp(key, "tileset") == 0) {
            if (m.tileset_count < MAX_TILESETS) {
                TileSet& ts = m.tilesets[m.tileset_count++];
                memset(&ts, 0, sizeof(ts));
                // "path,tile_w,tile_h,offset_x,offset_y"
                const char* p = val;
                int pi = 0;
                while (*p && !(*p == ',' && pi < 127)) {
                    ts.image_path[pi++] = *p++;
                }
                ts.image_path[pi] = '\0';
                if (*p == ',') ++p;
                sscanf(p, "%d,%d,%d,%d", &ts.tile_w, &ts.tile_h, &ts.offset_x, &ts.offset_y);
                ts.firstgid = 0;
                ts.columns  = 0;
            }
        }
        else if (state == S_LAYER && cur_layer) {
            if      (strcmp(key, "type") == 0) cur_layer->type = LayerTypeOf(val);
            else if (strcmp(key, "data") == 0) { reading_data = true; tile_row = 0; }
        }
        else if (state == S_ENEMY) {
            if (strcmp(key, "category") == 0) {
                strncpy(cur_enemy.category, val, sizeof(cur_enemy.category) - 1);
            } else if (strcmp(key, "location") == 0) {
                int x, y, w, h;
                if (sscanf(val, "%d,%d,%d,%d", &x, &y, &w, &h) == 4) {
                    cur_enemy.center_x = (float)x + (float)w * 0.5f;
                    cur_enemy.center_y = (float)y + (float)h * 0.5f;
                } else if (sscanf(val, "%d,%d", &x, &y) == 2) {
                    cur_enemy.center_x = (float)x;
                    cur_enemy.center_y = (float)y;
                }
            } else if (strcmp(key, "level") == 0) {
                cur_enemy.level = atoi(val);
            } else if (strcmp(key, "number") == 0) {
                int a, b;
                if (sscanf(val, "%d,%d", &a, &b) == 2) cur_enemy.number_min = a;
                else cur_enemy.number_min = atoi(val);
            } else if (strcmp(key, "wander_radius") == 0) {
                cur_enemy.wander_radius = atoi(val);
            }
        }
    }

    // Commit last enemy block.
    if (enemy_open && cur_enemy.category[0] && m.spawn_count < MAX_SPAWNS_PER_MAP) {
        m.spawns[m.spawn_count++] = cur_enemy;
    }

    fclose(f);
    return m.width > 0 && m.height > 0;
}

// ── TMX XML loader ────────────────────────────────────────────────────────────
// Custom strstr-based parser — no pugixml.
// Flare TMX has fixed structure: <map>, <tileset>, <layer>, <data encoding="csv">.

// Read attribute value as integer from a tag element string.
static int XmlAttrInt(const char* elem, const char* attr) {
    char pat[64];
    snprintf(pat, sizeof(pat), " %s=\"", attr);
    const char* p = strstr(elem, pat);
    if (!p) return 0;
    return atoi(p + strlen(pat));
}

// Copy attribute value into buf (returns true if found).
static bool XmlAttrStr(const char* elem, const char* attr, char* buf, int n) {
    char pat[64];
    snprintf(pat, sizeof(pat), " %s=\"", attr);
    const char* p = strstr(elem, pat);
    if (!p) { buf[0] = '\0'; return false; }
    p += strlen(pat);
    int i = 0;
    for (; *p && *p != '"' && i < n - 1; ++i, ++p) buf[i] = *p;
    buf[i] = '\0';
    return true;
}

// Copy tag element text up to '>' (or up to max_len).
static int XmlElemText(const char* tag_start, char* out, int max_len) {
    const char* end = strchr(tag_start, '>');
    int len = end ? (int)(end - tag_start) : max_len - 1;
    if (len >= max_len) len = max_len - 1;
    memcpy(out, tag_start, len);
    out[len] = '\0';
    return len;
}

static bool LoadTmx(const char* path, FlareMap& m) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    // 512 KB covers the largest known Flare TMX (104×104 × 3 layers ≈ 160 KB).
    static char buf[512 * 1024];
    int n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n <= 0) return false;
    buf[n] = '\0';

    memset(&m, 0, sizeof(m));

    // ── <map> ─────────────────────────────────────────────────────────────────
    const char* map_tag = strstr(buf, "<map ");
    if (!map_tag) return false;
    {
        char elem[512];
        XmlElemText(map_tag, elem, sizeof(elem));
        m.width  = XmlAttrInt(elem, "width");
        m.height = XmlAttrInt(elem, "height");
        m.tile_w = XmlAttrInt(elem, "tilewidth");
        m.tile_h = XmlAttrInt(elem, "tileheight");
    }

    // ── <properties> ─────────────────────────────────────────────────────────
    const char* props = strstr(buf, "<properties>");
    if (props) {
        const char* p = props;
        while ((p = strstr(p, "<property ")) != nullptr) {
            char pname[64] = {}, pval[128] = {};
            XmlAttrStr(p, "name",  pname, sizeof(pname));
            XmlAttrStr(p, "value", pval,  sizeof(pval));
            if      (strcmp(pname, "music")    == 0) CpStr(m.music_path,  sizeof(m.music_path),  pval);
            else if (strcmp(pname, "tileset")  == 0) CpStr(m.tileset_def, sizeof(m.tileset_def), pval);
            else if (strcmp(pname, "title")    == 0) CpStr(m.title,       sizeof(m.title),       pval);
            else if (strcmp(pname, "hero_pos") == 0) sscanf(pval, "%f,%f", &m.hero_x, &m.hero_y);
            p += 9;
        }
    }

    // ── <tileset> ─────────────────────────────────────────────────────────────
    const char* p = buf;
    while ((p = strstr(p, "<tileset ")) != nullptr) {
        if (m.tileset_count >= MAX_TILESETS) break;
        TileSet& ts = m.tilesets[m.tileset_count++];
        memset(&ts, 0, sizeof(ts));

        const char* tend = strstr(p, "</tileset>");
        if (!tend) tend = strstr(p, "/>");
        if (!tend) { ++p; continue; }

        char elem[1024];
        int elen = (int)(tend - p);
        if (elen >= (int)sizeof(elem)) elen = (int)sizeof(elem) - 1;
        memcpy(elem, p, elen);
        elem[elen] = '\0';

        ts.firstgid = XmlAttrInt(elem, "firstgid");
        ts.tile_w   = XmlAttrInt(elem, "tilewidth");
        ts.tile_h   = XmlAttrInt(elem, "tileheight");
        ts.columns  = XmlAttrInt(elem, "columns");

        const char* img = strstr(elem, "<image ");
        if (img) XmlAttrStr(img, "source", ts.image_path, sizeof(ts.image_path));

        p = tend + 1;
    }

    // ── <layer> ───────────────────────────────────────────────────────────────
    p = buf;
    while ((p = strstr(p, "<layer ")) != nullptr) {
        if (m.layer_count >= MAX_MAP_LAYERS) break;
        TileMapLayer& layer = m.layers[m.layer_count++];
        memset(&layer, 0, sizeof(layer));

        char lname[64] = {};
        XmlAttrStr(p, "name", lname, sizeof(lname));
        layer.type = LayerTypeOf(lname);

        const char* data_tag = strstr(p, "<data ");
        if (!data_tag) { ++p; continue; }
        const char* data_end = strstr(data_tag, "</data>");
        if (!data_end) { p = data_tag + 1; continue; }

        const char* csv = strchr(data_tag, '>');
        if (!csv || csv >= data_end) { p = data_tag + 1; continue; }
        ++csv;

        int count = 0;
        int max   = m.width * m.height;
        if (max > MAX_MAP_WIDTH * MAX_MAP_HEIGHT) max = MAX_MAP_WIDTH * MAX_MAP_HEIGHT;

        const char* q = csv;
        while (q < data_end && count < max) {
            while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t') ++q;
            if (!*q || q >= data_end) break;
            long v = strtol(q, (char**)&q, 10);
            int row = count / m.width;
            int col = count % m.width;
            if (row < MAX_MAP_HEIGHT && col < MAX_MAP_WIDTH)
                layer.tiles[row * MAX_MAP_WIDTH + col] = (uint16_t)(v > 0 ? v : 0);
            ++count;
            if (*q == ',') ++q;
        }

        p = data_end + 7;
    }

    return m.width > 0 && m.height > 0;
}

// ── Public API ─────────────────────────────────────────────────────────────────

bool LoadFlareMap(const char* path, FlareMap& out) {
    out.meta.Clear();

    const char* dot = strrchr(path, '.');
    if (!dot) return false;

    bool ok = (strcmp(dot, ".tmx") == 0) ? LoadTmx(path, out) : LoadTxt(path, out);
    if (!ok) return false;

    if (out.tileset_def[0])
        TryLoadTilesetDef(path, out.tileset_def, out.meta);

    out.meta.DumpFirst(10);
    return true;
}

bool ResolveTile(const FlareMap& map, uint16_t tile_id,
                 int* out_ts_idx, int* out_local_idx)
{
    if (tile_id == 0 || map.tileset_count == 0) return false;

    // Find the tileset whose firstgid range covers tile_id.
    // For .txt format (firstgid=0 on all tilesets), fall back to index 0.
    int best = -1;
    for (int i = 0; i < map.tileset_count; ++i) {
        int fg = map.tilesets[i].firstgid;
        if (fg == 0) { if (best < 0) best = i; continue; }
        if ((int)tile_id >= fg) {
            if (best < 0 || fg > map.tilesets[best].firstgid) best = i;
        }
    }
    if (best < 0) best = 0;

    *out_ts_idx   = best;
    *out_local_idx = (int)tile_id - map.tilesets[best].firstgid;
    if (*out_local_idx < 0) *out_local_idx = 0;
    return true;
}

} // namespace md::flare
