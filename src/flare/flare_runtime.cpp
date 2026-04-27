#include <monkey_dust/flare/flare_runtime.h>
#include <monkey_dust/flare/tile_map_renderer.h>
#include <monkey_dust/flare/tile_collision.h>
#include <monkey_dust/render/md_camera.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace md::flare {

// ── helpers ───────────────────────────────────────────────────────────────────

static bool IsFile(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Find the first .txt inside <dir> (non-recursive, alphabetic order).
static bool FirstTxtInDir(const char* dir, char* out, int out_sz) {
    DIR* d = opendir(dir);
    if (!d) return false;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        const char* ext = strrchr(ent->d_name, '.');
        if (!ext || strcmp(ext, ".txt") != 0) continue;
        snprintf(out, (size_t)out_sz, "%s/%s", dir, ent->d_name);
        closedir(d);
        return true;
    }
    closedir(d);
    return false;
}

// ── public API ────────────────────────────────────────────────────────────────

bool FlareRuntime::LoadMod(const char* mod_name, const char* mods_root,
                           const char* map_name, float tile_world_size) {
    loaded_ = false;
    chain_n_ = 0;
    tile_world_size_ = tile_world_size;

    // ── resolve mod chain ─────────────────────────────────────────────────────
    ModManager mm;
    mm.SetModsRoot(mods_root);
    if (!mm.Activate(mod_name)) {
        fprintf(stderr, "[FlareRuntime] Cannot activate mod '%s' from '%s'\n",
                mod_name, mods_root);
        return false;
    }

    // Build path array in child-first order (game mod → base mods).
    // ModManager chain is stored [dep0, ..., game_mod]; reverse it.
    int n = mm.Count();
    for (int i = n - 1; i >= 0 && chain_n_ < MAX_ACTIVE_MODS; --i) {
        const char* id = mm.GetModId(i);
        if (!id) continue;
        snprintf(chain_paths_[chain_n_], sizeof(chain_paths_[0]),
                 "%s/%s", mods_root, id);
        chain_ptrs_[chain_n_] = chain_paths_[chain_n_];
        ++chain_n_;
    }

    if (chain_n_ == 0) {
        fprintf(stderr, "[FlareRuntime] Empty mod chain for '%s'\n", mod_name);
        return false;
    }

    fprintf(stdout, "[FlareRuntime] Mod chain (%d):\n", chain_n_);
    for (int i = 0; i < chain_n_; ++i)
        fprintf(stdout, "  [%d] %s\n", i, chain_ptrs_[i]);

    // ── load map ──────────────────────────────────────────────────────────────
    memset(&map_, 0, sizeof(map_));
    bool map_loaded = false;

    if (map_name) {
        // Explicit map path (relative to mod root, e.g. "maps/perdition_harbor.txt")
        char full[512];
        if (mm.ResolveAsset(map_name, full, sizeof(full)))
            map_loaded = LoadFlareMap(full, map_);
        if (!map_loaded)
            fprintf(stderr, "[FlareRuntime] Cannot resolve map '%s'\n", map_name);
    } else {
        // Use first .txt found in maps/ of the game mod
        char maps_dir[512];
        snprintf(maps_dir, sizeof(maps_dir), "%s/maps", chain_ptrs_[0]);
        char first_map[512];
        if (FirstTxtInDir(maps_dir, first_map, sizeof(first_map)))
            map_loaded = LoadFlareMap(first_map, map_);
        if (!map_loaded)
            fprintf(stderr, "[FlareRuntime] No maps found in '%s'\n", maps_dir);
    }

    if (!map_loaded) return false;
    fprintf(stdout, "[FlareRuntime] Map loaded: %dx%d '%s'\n",
            map_.width, map_.height, map_.title);

    // ── load data registries ──────────────────────────────────────────────────
    memset(&enemies_, 0, sizeof(enemies_));
    memset(&items_,   0, sizeof(items_));
    memset(&powers_,  0, sizeof(powers_));
    memset(&factions_,0, sizeof(factions_));

    if (!LoadEnemies(chain_ptrs_, chain_n_, enemies_))
        fprintf(stderr, "[FlareRuntime] Warning: no enemies loaded\n");
    else
        fprintf(stdout, "[FlareRuntime] %d enemy types\n", enemies_.count);

    if (!LoadItems(chain_ptrs_, chain_n_, items_))
        fprintf(stderr, "[FlareRuntime] Warning: no items loaded\n");
    else
        fprintf(stdout, "[FlareRuntime] %d items\n", items_.count);

    if (!LoadPowers(chain_ptrs_, chain_n_, powers_))
        fprintf(stderr, "[FlareRuntime] Warning: no powers loaded\n");
    else
        fprintf(stdout, "[FlareRuntime] %d powers\n", powers_.count);

    BuildFactionsFromEnemies(enemies_, factions_);
    fprintf(stdout, "[FlareRuntime] %d factions derived\n", factions_.count);

    // ── build NavMesh ─────────────────────────────────────────────────────────
    if (!BuildNavMeshFromTileMap(map_, tile_world_size_))
        fprintf(stderr, "[FlareRuntime] Warning: NavMesh build failed\n");

    loaded_ = true;
    fprintf(stdout, "[FlareRuntime] LoadMod '%s' OK\n", mod_name);
    return true;
}

void FlareRuntime::Tick(float /*dt*/) {
    if (!loaded_) return;
    // Future: iterate sprite animation states and advance frame counters.
}

void FlareRuntime::Render(const MdCamera& cam, float aspect) {
    if (!loaded_) return;
    TileMapRenderer::Get().Render(map_, cam, aspect, tile_world_size_);
}

} // namespace md::flare
