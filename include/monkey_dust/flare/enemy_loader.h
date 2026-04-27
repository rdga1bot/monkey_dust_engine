#pragma once

namespace md::flare {

constexpr int MAX_ENEMY_TYPES = 256;

struct EnemyDef {
    char  id_str[64];         // filename stem  (e.g. "goblin")
    char  name[64];
    char  animations[128];
    char  categories[128];    // raw comma-separated  (e.g. "goblin,humanoid")
    int   level;
    int   hp;
    float speed;              // tiles / second
    int   xp;
    int   dmg_melee_min, dmg_melee_max;
    int   dmg_ment_min,  dmg_ment_max;
    int   dmg_ranged_min, dmg_ranged_max;
    int   absorb_min, absorb_max;
    int   accuracy, avoidance;
    float melee_range, threat_range;
    int   turn_delay_ms;
    bool  humanoid;
    bool  valid;
};

struct EnemyRegistry {
    EnemyDef defs[MAX_ENEMY_TYPES];
    int      count;
};

// Scan <mod_chain[0]>/enemies/ recursively; parse every .txt into an EnemyDef.
// INCLUDE directives are resolved by searching each path in mod_chain in order,
// supporting the Flare mod-inheritance model (child mod → base mod).
// n must be ≥ 1.  Returns false only when enemies/ cannot be opened in chain[0].
bool LoadEnemies(const char* const* mod_chain, int n, EnemyRegistry& out);

// Convenience: single-mod load (no base mod).
inline bool LoadEnemies(const char* mod_path, EnemyRegistry& out) {
    return LoadEnemies(&mod_path, 1, out);
}

// Linear search by id_str (filename stem).
const EnemyDef* FindEnemy(const EnemyRegistry& reg, const char* id_str);

} // namespace md::flare
