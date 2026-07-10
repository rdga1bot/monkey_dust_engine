#include <monkey_dust/world/biome_def.h>
#include <monkey_dust/platform/md_log.h>
#include <cstdio>
#include <cstring>

// Text format (one directive per line):
//   tex_count <N>
//   tex <index>|<diffuse_path>|<normal_path>   ('|'-delimited: real Kenshi
//       filenames can contain spaces, e.g. "Smallish sharpGravel_DIF.dds" --
//       confirmed by direct crash repro: whitespace-split parsing silently
//       truncated that one path, InitFromDDSArray failed for ALL layers at
//       once as a result, and the renderer segfaulted on the first frame.)
//   biome_count <N>
//   biome <slug> <tex_base> <tex_slope> <tex_cliff> <tex_grass> <tex_dirt> <tex_road>
//               <fog_r> <fog_g> <fog_b> <sky_r> <sky_g> <sky_b>
//               <legend_r> <legend_g> <legend_b>
// Lines starting with '#' and blank lines are ignored. Slugs must not
// contain spaces (biome lines are still whitespace-split). This is a
// generic parser — the real biome data lives in a private file outside
// this repo (see the private generator that emits this format for the
// authoritative field-by-field rationale).

bool BiomeRegistry::LoadFromFile(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "[BiomeRegistry] cannot open %s — biome data not loaded", path);
        return false;
    }

    tex_count_   = 0;
    biome_count_ = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        if (!strncmp(p, "tex_count", 9)) {
            continue; // informational only; arrays are fixed-size
        }
        if (!strncmp(p, "tex ", 4)) {
            // '|'-delimited (not whitespace/sscanf %s): real filenames can
            // contain spaces, see format-comment above for the crash this fixed.
            char* rest = p + 4;
            char* bar1 = strchr(rest, '|');
            char* bar2 = bar1 ? strchr(bar1 + 1, '|') : nullptr;
            if (bar1 && bar2) {
                *bar1 = '\0'; *bar2 = '\0';
                int idx = atoi(rest);
                char* dif = bar1 + 1;
                char* nml = bar2 + 1;
                for (char* c = nml; *c; ++c) if (*c == '\n' || *c == '\r') { *c = '\0'; break; }
                if (idx >= 0 && idx < MAX_TEXTURES) {
                    strncpy(tex_paths_[idx], dif, MAX_PATH_LEN - 1);
                    strncpy(nml_paths_[idx], nml, MAX_PATH_LEN - 1);
                    if (idx + 1 > tex_count_) tex_count_ = idx + 1;
                }
            }
            continue;
        }
        if (!strncmp(p, "biome_count", 11)) {
            continue;
        }
        if (!strncmp(p, "biome ", 6)) {
            if (biome_count_ >= MAX_BIOMES) continue;
            BiomeEntry& e = biomes_[biome_count_];
            char slug[MAX_SLUG_LEN] = {0};
            BiomeDef& d = e.def;
            int lr = 0, lg = 0, lb = 0;
            int n = sscanf(p + 6, "%47s %d %d %d %d %d %d %f %f %f %f %f %f %d %d %d",
                slug, &d.tex_base, &d.tex_slope, &d.tex_cliff,
                &d.tex_grass, &d.tex_dirt, &d.tex_road,
                &d.fog_r, &d.fog_g, &d.fog_b,
                &d.sky_horizon_r, &d.sky_horizon_g, &d.sky_horizon_b,
                &lr, &lg, &lb);
            if (n == 16) {
                strncpy(e.slug, slug, MAX_SLUG_LEN - 1);
                e.legend_rgb[0] = (uint8_t)lr;
                e.legend_rgb[1] = (uint8_t)lg;
                e.legend_rgb[2] = (uint8_t)lb;
                ++biome_count_;
            }
            continue;
        }
    }
    fclose(f);

    if (biome_count_ > 0) default_ = biomes_[0].def;
    fprintf(stdout, "[BiomeRegistry] loaded %d biomes, %d ground textures from %s\n",
            biome_count_, tex_count_, path);
    return biome_count_ > 0;
}

const BiomeDef& BiomeRegistry::ForZone(const char* zone_slug) const {
    if (!zone_slug || !zone_slug[0]) return default_;
    for (int i = 0; i < biome_count_; ++i)
        if (!strcmp(biomes_[i].slug, zone_slug)) return biomes_[i].def;
    return default_;
}

const BiomeDef& BiomeRegistry::ForColor(uint8_t r, uint8_t g, uint8_t b) const {
    if (biome_count_ == 0) return default_;
    int best = 0, best_d2 = 0x7FFFFFFF;
    for (int i = 0; i < biome_count_; ++i) {
        int dr = (int)r - (int)biomes_[i].legend_rgb[0];
        int dg = (int)g - (int)biomes_[i].legend_rgb[1];
        int db = (int)b - (int)biomes_[i].legend_rgb[2];
        int d2 = dr*dr + dg*dg + db*db;
        if (d2 < best_d2) { best_d2 = d2; best = i; }
    }
    return biomes_[best].def;
}

const char* BiomeRegistry::GroundTexPath(int idx) const {
    if (idx < 0 || idx >= tex_count_) return "";
    return tex_paths_[idx];
}

const char* BiomeRegistry::GroundNmlPath(int idx) const {
    if (idx < 0 || idx >= tex_count_) return "";
    return nml_paths_[idx];
}
