#pragma once
#include <monkey_dust/flare/tile_map.h>
#include <monkey_dust/flare/enemy_loader.h>
#include <monkey_dust/flare/item_loader.h>
#include <monkey_dust/flare/power_loader.h>
#include <monkey_dust/flare/faction_loader.h>
#include <monkey_dust/flare/mod_manager.h>

namespace md {
struct MdCamera;
}

namespace md::flare {

// Orchestrates the full Flare mod lifecycle:
//   LoadMod → mod chain, map, enemies, items, powers, factions, NavMesh
//   Tick    → sprite animation updates  (stub until ECS adapters are wired)
//   Render  → TileMapRenderer draw call
//
// All data registries live as members of this singleton (BSS, no heap).
class FlareRuntime {
public:
    static FlareRuntime& Get() {
        static FlareRuntime inst;
        return inst;
    }

    // Activate <mod_name> from <mods_root>.  Resolves the full mod chain
    // (via ModManager), loads the first map found in maps/ (or the explicitly
    // named <map_name>, relative path e.g. "maps/perdition_harbor.txt"),
    // populates all data registries, and builds the NavMesh.
    //
    // tile_world_size: world-space footprint per tile; must match the value
    // passed to Render() so nav geometry and visuals are in sync.
    // Returns false on any fatal failure.
    bool LoadMod(const char* mod_name, const char* mods_root,
                 const char* map_name       = nullptr,
                 float       tile_world_size = 1.0f);

    // Advance sprite animation state (future ECS hook point).
    void Tick(float dt);

    // Draw the current tile map via TileMapRenderer.
    void Render(const MdCamera& cam, float aspect);

    bool  IsLoaded()       const { return loaded_; }
    float TileWorldSize()  const { return tile_world_size_; }

    const FlareMap&              GetMap()      const { return map_; }
    const EnemyRegistry&         GetEnemies()  const { return enemies_; }
    const ItemRegistry&          GetItems()    const { return items_; }
    const PowerRegistry&         GetPowers()   const { return powers_; }
    const FlareFactionRegistry&  GetFactions() const { return factions_; }

private:
    FlareRuntime() = default;

    // Resolved full paths for each mod (child-first for INCLUDE search).
    char        chain_paths_[MAX_ACTIVE_MODS][512];
    const char* chain_ptrs_ [MAX_ACTIVE_MODS];
    int         chain_n_     = 0;

    FlareMap             map_;
    EnemyRegistry        enemies_;
    ItemRegistry         items_;
    PowerRegistry        powers_;
    FlareFactionRegistry factions_;
    float                tile_world_size_ = 1.0f;
    bool                 loaded_          = false;
};

} // namespace md::flare
