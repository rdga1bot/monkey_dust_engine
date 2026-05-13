#pragma once

// ── MapManager ────────────────────────────────────────────────────────────────
// CATHODE GAME_LEVEL_MANAGER analog.
// Singleton. Loads a manifest of map names; queues transitions.
// Actual map load/unload is done by the game layer via registered callbacks.
//
// Transition flow:
//   QueueMap(name)           — set queued_idx_ (replaces any previous queued)
//   RequestTransition(force) — set pending_ = true
//   Game polls IsPendingTransition() each tick; executes MapLoadCb then clears.

static constexpr int MAP_MAX_MAPS     = 16;
static constexpr int MAP_NAME_MAX_LEN = 64;
static constexpr int MAP_PATH_MAX_LEN = 128;

// Callback signatures — set by game layer before any transition.
// load_cb(map_name, force) — triggered when RequestTransition fires.
using MapLoadCb = void(*)(const char* map_name, bool force);

class MapManager {
public:
    static MapManager& Get();

    // Load data/maps/manifest.json — non-fatal if missing (maps remain empty).
    bool LoadManifest(const char* json_path);

    // Register load callback; must be set before RequestTransition().
    void SetLoadCallback(MapLoadCb cb) { load_cb_ = cb; }

    // Queue a map by name (replaces existing queue).
    // Returns false if name not found in manifest.
    bool QueueMap(const char* name);

    // Queue by manifest index.
    bool QueueMapByIndex(int idx);

    // Mark transition as requested. Stores force flag.
    // Game loop calls ExecuteTransition() when ready.
    void RequestTransition(bool force = false);

    // Execute the pending transition: calls load_cb_, clears pending state.
    // Typically called from game side at end of logic tick.
    void ExecuteTransition();

    bool        IsPendingTransition()  const { return pending_; }
    const char* GetCurrentMapName()    const;
    const char* GetQueuedMapName()     const;
    int         GetCurrentIndex()      const { return current_idx_; }
    int         GetMapCount()          const { return map_count_; }

    // Lookup index by name; returns -1 if not found.
    int FindMap(const char* name) const;

private:
    MapManager() = default;

    char     map_names_[MAP_MAX_MAPS][MAP_NAME_MAX_LEN] = {};
    int      map_count_   = 0;
    int      current_idx_ = 0;
    int      queued_idx_  = -1;
    bool     pending_     = false;
    bool     force_       = false;
    MapLoadCb load_cb_    = nullptr;
};
