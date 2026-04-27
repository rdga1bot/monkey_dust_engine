#include <monkey_dust/flare/mod_manager.h>
#include <monkey_dust/flare/ini_parser.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

namespace md::flare {

// ── helpers ──────────────────────────────────────────────────────────────────

static bool FileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool DirExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// ── internal ─────────────────────────────────────────────────────────────────

void ModManager::BuildPath(char* out, int out_size,
                           const char* mod_id, const char* relative) const
{
    snprintf(out, (size_t)out_size, "%s/%s/%s", root_, mod_id, relative);
}

// Parse requires= lines from settings.txt into a small list.
static int ReadRequires(const char* settings_path,
                        char out[][MAX_MOD_ID], int max)
{
    struct S { char (*arr)[MAX_MOD_ID]; int count; int max; };
    S s = { out, 0, max };
    ParseIniFile(settings_path,
        [](const IniLine& l, void* ud) {
            S* s2 = (S*)ud;
            if (strcmp(l.key, "requires") != 0) return;
            if (s2->count < s2->max) {
                // Strip optional version suffix: "fantasycore:1.14" → "fantasycore"
                strncpy(s2->arr[s2->count], l.value, MAX_MOD_ID - 1);
                s2->arr[s2->count][MAX_MOD_ID - 1] = '\0';
                char* colon = strchr(s2->arr[s2->count], ':');
                if (colon) *colon = '\0';
                ++s2->count;
            }
        }, &s, nullptr);
    return s.count;
}

bool ModManager::ActivateOne(const char* mod_id, int depth) {
    if (depth > MAX_ACTIVE_MODS) return false;

    // Already in chain?
    for (int i = 0; i < count_; ++i)
        if (strcmp(chain_[i], mod_id) == 0) return true;

    // Mod directory must exist.
    char dir[MAX_MOD_PATH];
    snprintf(dir, sizeof(dir), "%s/%s", root_, mod_id);
    if (!DirExists(dir)) return false;

    // Pull in requirements first (depth-first).
    char settings_path[MAX_MOD_PATH];
    snprintf(settings_path, sizeof(settings_path), "%s/settings.txt", dir);
    if (FileExists(settings_path)) {
        char reqs[MAX_ACTIVE_MODS][MAX_MOD_ID];
        int  n = ReadRequires(settings_path, reqs, MAX_ACTIVE_MODS);
        for (int i = 0; i < n; ++i)
            ActivateOne(reqs[i], depth + 1);
    }

    // Append this mod after its dependencies.
    if (count_ < MAX_ACTIVE_MODS) {
        strncpy(chain_[count_], mod_id, MAX_MOD_ID - 1);
        chain_[count_][MAX_MOD_ID - 1] = '\0';
        ++count_;
        return true;
    }
    return false;
}

// ── public API ───────────────────────────────────────────────────────────────

bool ModManager::Activate(const char* mod_id) {
    return ActivateOne(mod_id, 0);
}

void ModManager::Clear() {
    count_ = 0;
}

void ModManager::SetModsRoot(const char* root) {
    strncpy(root_, root, sizeof(root_) - 1);
    root_[sizeof(root_) - 1] = '\0';
}

int ModManager::Count() const { return count_; }

const char* ModManager::GetModId(int i) const {
    return (i >= 0 && i < count_) ? chain_[i] : nullptr;
}

bool ModManager::ResolveAsset(const char* relative_path,
                               char*       out,
                               int         out_size) const
{
    // Search chain back-to-front: latest mod wins.
    for (int i = count_ - 1; i >= 0; --i) {
        BuildPath(out, out_size, chain_[i], relative_path);
        if (FileExists(out)) return true;
    }
    out[0] = '\0';
    return false;
}

// Simple basename extraction (up to last '/').
static const char* Basename(const char* path) {
    const char* last = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/') last = p + 1;
    return last;
}

void ModManager::ForEachAsset(const char* relative_dir,
                               AssetCallback cb,
                               void*         user_data) const
{
    // Track visited basenames (de-duplicate across mods).
    // Use a fixed table: up to 512 filenames, each ≤ 128 bytes.
    static constexpr int  MAX_VISITED = 512;
    static constexpr int  NAME_LEN    = 128;
    static char visited[MAX_VISITED][NAME_LEN];
    int         visited_count = 0;

    // Iterate chain back-to-front so higher-priority mods are processed first.
    for (int i = count_ - 1; i >= 0; --i) {
        char dir_path[MAX_MOD_PATH];
        BuildPath(dir_path, sizeof(dir_path), chain_[i], relative_dir);

        DIR* d = opendir(dir_path);
        if (!d) continue;

        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            if (ent->d_name[0] == '.') continue;

            // Check if we've already visited this basename.
            bool seen = false;
            for (int v = 0; v < visited_count; ++v) {
                if (strcmp(visited[v], ent->d_name) == 0) { seen = true; break; }
            }
            if (seen) continue;

            if (visited_count < MAX_VISITED) {
                strncpy(visited[visited_count], ent->d_name, NAME_LEN - 1);
                visited[visited_count][NAME_LEN - 1] = '\0';
                ++visited_count;
            }

            char full[MAX_MOD_PATH * 2];
            snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
            if (FileExists(full)) cb(full, user_data);
        }
        closedir(d);
    }
}

} // namespace md::flare
