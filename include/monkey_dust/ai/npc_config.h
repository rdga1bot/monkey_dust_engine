#pragma once

// ── NpcConfig ─────────────────────────────────────────────────────────────────
// Per-NPC-type configuration loaded from data/characters/npc_configs.json.
// CATHODE: equivalent to per-entity property bundles referenced by EntityInterface.
// $inherit: one level only — child inherits base, then overrides non-zero/non-empty fields.
struct NpcConfig {
    char  name[32];
    char  parent[32];          // $inherit base name; empty = no parent
    char  cone_set[24];        // SenseRegistry name (e.g. "HUMAN")
    char  director_profile[24];// DirectorSystem profile name (e.g. "default")
    char  bt_day[32];          // BT template id for daytime behaviour
    char  bt_night[32];        // BT template id for nighttime behaviour
    // Pattern 9: per-stage BT override (CATHODE ALIENCONFIGS per-mood analog).
    // If bt_stage[i] is non-empty, DirectorSystem activates it when stage == i.
    // Stage order: 0=Unaware 1=Suspicious 2=Hunting 3=Intense.
    // Empty string = fall back to bt_day/bt_night.
    char  bt_stage[4][32];
    int   max_health;
    float move_speed;          // world units/sec
    float wander_radius;       // tile-radius wander zone
};

// ── NpcConfigRegistry ─────────────────────────────────────────────────────────
// Singleton. Loads npc_configs.json with 2-pass $inherit resolution.
// Max NPC types = 32 (fits typical RPG roster, fixed BSS).
static constexpr int MAX_NPC_CONFIGS = 32;

class NpcConfigRegistry {
public:
    static NpcConfigRegistry& Get();

    // 2-pass load: Pass1 = parse all; Pass2 = resolve $inherit.
    // Returns false on file error (non-fatal — game can still run).
    bool Load(const char* json_path);

    // Returns nullptr if not found (logs warning).
    const NpcConfig* Find(const char* name) const;

    int Count() const { return count_; }

private:
    NpcConfig configs_[MAX_NPC_CONFIGS] = {};
    int       count_                    = 0;
};
