#pragma once

namespace md::flare {

constexpr int MAX_ACTIVE_MODS = 8;
constexpr int MAX_MOD_PATH    = 256;
constexpr int MAX_MOD_ID      = 64;

// Resolves an ordered chain of Flare mods: default → fantasycore → empyrean_campaign.
// Override rule: a mod listed later in the chain wins on identical relative paths.
//
// Data lives under "game/data/mods/<mod_id>/".
class ModManager {
public:
    // Activate mod_id, recursively pulling in required mods first.
    // settings.txt in the mod dir may contain "requires=<other_mod>".
    // Returns false if mod directory doesn't exist.
    bool Activate(const char* mod_id);

    void Clear();

    // Resolve relative asset path → absolute path of the winning override.
    // Example: ResolveAsset("enemies/goblin.txt") searches chain back-to-front
    // and returns the first existing file. Returns false if not found in any mod.
    bool ResolveAsset(const char* relative_path,
                      char*       out_full_path,
                      int         out_size) const;

    // Iterate all files under relative_dir across the chain (duplicates suppressed
    // — only the highest-priority (latest) mod's copy is visited per filename).
    using AssetCallback = void(*)(const char* full_path, void* user_data);
    void ForEachAsset(const char* relative_dir,
                      AssetCallback cb,
                      void*         user_data) const;

    // Number of mods in active chain.
    int Count() const;

    // Base root for all mods (default: "game/data/mods").
    // Override for unit testing.
    void SetModsRoot(const char* root);

private:
    char chain_[MAX_ACTIVE_MODS][MAX_MOD_ID];
    int  count_    = 0;
    char root_[MAX_MOD_PATH] = "game/data/mods";

    bool ActivateOne(const char* mod_id, int depth);
    void BuildPath(char* out, int out_size,
                   const char* mod_id, const char* relative) const;
};

} // namespace md::flare
