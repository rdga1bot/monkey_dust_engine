#pragma once
#include <cstdint>
#include <cstring>

// ── MdPrefab ─────────────────────────────────────────────────────────────────
// Pure-data archetype descriptor for an NPC type.
// Loaded from data/prefabs/prefabs.json at startup; used by spawn code to
// configure HP, BT template, combat profile, and wander radius without
// hardcoding per-category logic in C++.
//
// JSON format (array of objects):
//   [
//     {
//       "name":           "guard",
//       "bt_template":    "guard",
//       "hp":             100.0,
//       "wander_radius":  5.0,
//       "combat_profile": "bandit",  // "bandit" | "trader" | "none"
//       "alliance_group": 3,         // integer cast of AllianceGroup enum
//       "is_aggressive":  true
//     },
//     ...
//   ]

static constexpr int MD_PREFAB_MAX        = 32;
static constexpr int MD_PREFAB_NAME_LEN   = 64;
static constexpr int MD_PREFAB_TMPL_LEN   = 64;

// combat_profile values (mirrors game-side Combat::Make* without depending on game/)
static constexpr uint8_t MD_COMBAT_BANDIT = 0;
static constexpr uint8_t MD_COMBAT_TRADER = 1;
static constexpr uint8_t MD_COMBAT_NONE   = 2;

struct MdPrefab {
    char    name        [MD_PREFAB_NAME_LEN] = {};
    char    bt_template [MD_PREFAB_TMPL_LEN] = {};  // BTLoader builder name
    float   hp              = 50.0f;
    float   wander_radius   = 3.0f;
    uint8_t combat_profile  = MD_COMBAT_BANDIT; // 0=bandit, 1=trader, 2=none
    uint8_t alliance_group  = 0;                // cast of AllianceGroup
    bool    is_aggressive   = false;
    uint8_t _pad[1]         = {};
};

// ── MdPrefabRegistry ─────────────────────────────────────────────────────────
// Singleton flat-array store of named NPC archetypes.
// Register at startup (via LoadFromJson) before the first spawn call.
// No malloc — fixed MdPrefab[MD_PREFAB_MAX] BSS array.
class MdPrefabRegistry {
public:
    static MdPrefabRegistry& Get() {
        static MdPrefabRegistry inst;
        return inst;
    }

    // Add or update a prefab by name.
    bool Register(const MdPrefab& p) noexcept {
        if (!p.name[0]) return false;
        for (int i = 0; i < count_; ++i) {
            if (strcmp(prefabs_[i].name, p.name) == 0) {
                prefabs_[i] = p;
                return true;
            }
        }
        if (count_ >= MD_PREFAB_MAX) return false;
        prefabs_[count_++] = p;
        return true;
    }

    // Find by name; returns nullptr if not found.
    const MdPrefab* Find(const char* name) const noexcept {
        if (!name || !name[0]) return nullptr;
        for (int i = 0; i < count_; ++i)
            if (strcmp(prefabs_[i].name, name) == 0) return &prefabs_[i];
        return nullptr;
    }

    // Parse JSON array of prefab objects (strstr-based, no external library).
    // Returns number of prefabs registered (≥0); -1 on null input.
    int LoadFromJson(const char* json) noexcept;

    void Clear()  noexcept { count_ = 0; }
    int  Count()  const noexcept { return count_; }

private:
    MdPrefabRegistry() = default;
    MdPrefab prefabs_[MD_PREFAB_MAX] = {};
    int      count_ = 0;
};
