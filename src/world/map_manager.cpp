#include <monkey_dust/world/map_manager.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

MapManager& MapManager::Get() {
    static MapManager inst;
    return inst;
}

const char* MapManager::GetCurrentMapName() const {
    if (current_idx_ < 0 || current_idx_ >= map_count_) return "";
    return map_names_[current_idx_];
}

const char* MapManager::GetQueuedMapName() const {
    if (queued_idx_ < 0 || queued_idx_ >= map_count_) return "";
    return map_names_[queued_idx_];
}

int MapManager::FindMap(const char* name) const {
    for (int i = 0; i < map_count_; ++i)
        if (strncmp(map_names_[i], name, MAP_NAME_MAX_LEN) == 0) return i;
    return -1;
}

bool MapManager::QueueMap(const char* name) {
    int idx = FindMap(name);
    if (idx < 0) {
        MD_LOG(MD_LOG_WARNING, "MapManager: map '%s' not in manifest", name);
        return false;
    }
    queued_idx_ = idx;
    return true;
}

bool MapManager::QueueMapByIndex(int idx) {
    if (idx < 0 || idx >= map_count_) {
        MD_LOG(MD_LOG_WARNING, "MapManager: index %d out of range (%d maps)", idx, map_count_);
        return false;
    }
    queued_idx_ = idx;
    return true;
}

void MapManager::RequestTransition(bool force) {
    if (queued_idx_ < 0) {
        MD_LOG(MD_LOG_WARNING, "MapManager: RequestTransition with no queued map");
        return;
    }
    pending_ = true;
    force_   = force;
}

void MapManager::ExecuteTransition() {
    if (!pending_ || queued_idx_ < 0) return;
    if (!load_cb_) {
        MD_LOG(MD_LOG_WARNING, "MapManager: no load callback set — transition skipped");
        pending_ = false;
        return;
    }
    current_idx_ = queued_idx_;
    queued_idx_  = -1;
    pending_     = false;
    load_cb_(map_names_[current_idx_], force_);
    force_ = false;
}

// ── LoadManifest ──────────────────────────────────────────────────────────────
// Parses: { "maps": ["map_a", "map_b", ...] }

bool MapManager::LoadManifest(const char* json_path) {
    map_count_ = 0;

    FILE* f = fopen(json_path, "rb");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "MapManager: cannot open manifest '%s'", json_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 16384) {
        fclose(f);
        MD_LOG(MD_LOG_WARNING, "MapManager: manifest too large or empty");
        return false;
    }
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(sz) + 1));
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, static_cast<size_t>(sz), f);
    buf[sz] = '\0';
    fclose(f);

    // Find "maps" array
    const char* arr = strstr(buf, "\"maps\"");
    if (!arr) { free(buf); return false; }
    arr = strstr(arr, "[");
    if (!arr) { free(buf); return false; }
    const char* arr_end = strstr(arr, "]");

    const char* p = arr + 1;
    while (map_count_ < MAP_MAX_MAPS) {
        // Find next quoted string
        const char* q = strstr(p, "\"");
        if (!q || (arr_end && q >= arr_end)) break;
        ++q;
        int i = 0;
        while (*q && *q != '"' && i < MAP_NAME_MAX_LEN - 1)
            map_names_[map_count_][i++] = *q++;
        map_names_[map_count_][i] = '\0';
        if (i > 0) ++map_count_;
        p = (*q == '"') ? q + 1 : q;
    }

    free(buf);
    MD_LOG(MD_LOG_INFO, "MapManager: loaded %d maps from '%s'", map_count_, json_path);
    return map_count_ > 0;
}
