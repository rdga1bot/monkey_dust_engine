#pragma once
#include <monkey_dust/world/chunk_def.h>
#include <atomic>
#include <cstdint>

// ─────────────────────────────────────────────────────────
// POD staging buffer for two-phase async chunk loading.
//
// Phase 1 (IO worker):  fread / LCG → fill StagedNpc/StagedTree
// Phase 2 (main thread): ApplyStagedChunks → reg.create() + emplace
//
// No EnTT types here — pure POD, safe to fill from any thread.
// ─────────────────────────────────────────────────────────

struct StagedNpc {
    float    x, z;
    float    hp_current, hp_max;
    uint32_t faction_id;
    uint8_t  weapon_type;  // 0=Fists 1=Sword 2=Spear
    uint8_t  armor_class;  // 0=None 1=Leather 2=Chain 3=Plate
    uint8_t  is_bandit;    // → BuildCombatBT vs BuildWanderBT
    uint8_t  _pad;
};

struct StagedTree {
    float x, z, scale;
};

struct ChunkLoadStaging {
    ChunkCoord coord;
    StagedNpc  npcs [64];
    int        npc_count;
    StagedTree trees[256];
    int        tree_count;
    std::atomic<bool> ready{false};   // worker sets true when done
    bool              consumed{false}; // main sets true after apply
};

// 32 slots: covers up to chunk_radius=5 initial load (~21 new chunks per step)
// without silently dropping requests. Previous value of 8 was too small.
static constexpr int MAX_STAGING = 32;
