#pragma once
#include <cstdint>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/world/world_transform.h>

// OffscreenNpcDatabase — 44B-per-slot bulk storage for NPCs outside loaded chunks.
// Live NPCs are Captured on chunk-unload → stored here as lightweight state.
// On chunk-load near player, Spawn() re-materialises them as full ECS entities.
// Tick() runs 1 Hz bulk: hunger++, fatigue++, patrol drift.

static constexpr int MAX_OFFSCREEN_NPCS = 10000;

enum class OffscreenTask : uint8_t {
    Patrol = 0,
    Work   = 1,
    Rest   = 2,
    Trade  = 3,
};

struct OffscreenNpcState {
    float    position[3];                    // world-space XYZ           12B
    uint32_t entity_uid;                     // for respawn continuity     4B
    uint32_t faction_id;                     // faction index              4B
    uint16_t hp_head, hp_torso;              // limb HP (packed /10)       4B
    uint16_t hp_larm, hp_rarm;              //                             4B
    uint16_t hp_lleg, hp_rleg;              //                             4B
    uint8_t  hunger;                         // 0=full 255=starving        1B
    uint8_t  fatigue;                        // 0=fresh 255=exhausted      1B
    uint8_t  alertness;                      // 0=idle 255=max alert       1B
    OffscreenTask task_type;                 //                             1B
    uint16_t zone_id;                        // zone index                 2B
    uint16_t _pad;                           //                             2B
};                                           //                     total 40B
static_assert(sizeof(OffscreenNpcState) == 40, "OffscreenNpcState size mismatch");

class OffscreenNpcDatabase {
public:
    static OffscreenNpcDatabase& Get() {
        static OffscreenNpcDatabase inst;
        return inst;
    }

    // 1 Hz bulk tick: schedule-aware hunger/fatigue + patrol drift.
    // game_hours ∈ [0, 24): night (18..6) → rest mode (fatigue recovery).
    void Tick(float dt, float game_hours = 12.f) noexcept;

    // Materialise offscreen NPCs whose zone is being loaded near (px, pz).
    // Re-creates ECS entities; removes them from the offscreen pool.
    void Spawn(entt::registry& reg, float player_x, float player_z,
               float spawn_radius_m = 600.f);

    // Dematerialise a live ECS entity (chunk unload or LOD cull).
    // Copies WorldTransform, LimbHealth, NpcNeeds → compact slot.
    // Does not destroy the entity — caller is responsible.
    bool Capture(entt::entity e, entt::registry& reg, uint16_t zone_id);

    // Remove all offscreen NPCs belonging to a given zone.
    void PurgeZone(uint16_t zone_id) noexcept;

    // Zone NPC persistence — saves/zone_NNNN.onpc binary files.
    // SaveZone: write entries for zone_id → saves_dir/zone_NNNN.onpc.
    //   Call BEFORE PurgeZone so data is not lost on zone shift.
    // LoadZone: read file and append entries (skip duplicates by entity_uid).
    //   Returns number of entries loaded; 0 if file not found.
    // LoadAllZones: scan saves_dir for zone_*.onpc and call LoadZone for each.
    //   Call once at startup to restore world NPC state from last session.
    bool SaveZone(uint16_t zone_id, const char* saves_dir) noexcept;
    int  LoadZone(uint16_t zone_id, const char* saves_dir) noexcept;
    int  LoadAllZones(const char* saves_dir) noexcept;

    int  Count()        const noexcept { return count_; }
    bool IsFull()       const noexcept { return count_ >= MAX_OFFSCREEN_NPCS; }

    const OffscreenNpcState* Data()  const noexcept { return entries_; }

private:
    OffscreenNpcDatabase() = default;

    // Remove entry at index i (swap-erase).
    void Remove(int i) noexcept {
        entries_[i] = entries_[--count_];
    }

    static constexpr float PATROL_DRIFT_M = 5.f;   // max positional drift per tick
    static constexpr float HUNGER_RATE    = 1.f;    // hunger units/tick at 1Hz
    static constexpr float FATIGUE_RATE   = 0.5f;   // fatigue units/tick

    OffscreenNpcState entries_[MAX_OFFSCREEN_NPCS];
    int               count_ = 0;
    uint32_t          uid_counter_ = 1;     // monotonic; 0 = invalid
};
