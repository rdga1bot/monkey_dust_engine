#pragma once
// ZoneState — per-zone persistent data.
// Extends ZoneActivityMap (visit/dirty flags) with gameplay state.
// Kenshi pattern: each zone has biome, danger level, faction control,
//   and a timestamp for offscreen simulation delta-tick.
//
// Storage: ZoneStateRegistry singleton (flat array, MAX_ZONES=512).
// Serialized to saves/zone_state.bin alongside zone_activity.bin.

#include <cstdint>
#include <cstring>

static constexpr int ZS_MAX_ZONES = 512;

struct ZoneState {
    uint32_t zone_id;                  // matches terrain_config.txt zone_id   4B
    uint32_t controlling_faction_id;   // 0 = unclaimed                        4B
    float    world_time_last_visited;  // game_time_hours when player was here  4B
    uint32_t entity_uid_base;          // respawn UID range base                4B
    uint8_t  biome;                    // BiomeType enum                        1B
    uint8_t  danger_level;             // 0-10                                  1B
    uint8_t  flags;                    // bit0=visited, bit1=cleared, bit2=settlement 1B
    uint8_t  _pad;                     //                                       1B
};                                     //                               total  20B
static_assert(sizeof(ZoneState) == 20, "ZoneState must be 20 bytes");

namespace ZoneStateFlag {
    constexpr uint8_t Visited    = 1u << 0;
    constexpr uint8_t Cleared    = 1u << 1;  // hostile NPCs killed
    constexpr uint8_t Settlement = 1u << 2;  // has a player settlement
}

class ZoneStateRegistry {
public:
    static ZoneStateRegistry& Get() noexcept {
        static ZoneStateRegistry inst;
        return inst;
    }

    void Reset() noexcept { count_ = 0; }

    ZoneState* Find(uint32_t zone_id) noexcept {
        for (int i = 0; i < count_; ++i)
            if (zones_[i].zone_id == zone_id) return &zones_[i];
        return nullptr;
    }

    const ZoneState* Find(uint32_t zone_id) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (zones_[i].zone_id == zone_id) return &zones_[i];
        return nullptr;
    }

    // Find or create; returns nullptr if registry is full.
    ZoneState* FindOrCreate(uint32_t zone_id) noexcept {
        ZoneState* s = Find(zone_id);
        if (s) return s;
        if (count_ >= ZS_MAX_ZONES) return nullptr;
        ZoneState& n = zones_[count_++];
        memset(&n, 0, sizeof(n));
        n.zone_id = zone_id;
        return &n;
    }

    void MarkVisited(uint32_t zone_id, float game_time) noexcept {
        ZoneState* s = FindOrCreate(zone_id);
        if (!s) return;
        s->flags |= ZoneStateFlag::Visited;
        s->world_time_last_visited = game_time;
    }

    bool Save(const char* path) const noexcept;
    bool Load(const char* path) noexcept;

    int Count() const noexcept { return count_; }

private:
    ZoneStateRegistry() { memset(zones_, 0, sizeof(zones_)); }

    ZoneState zones_[ZS_MAX_ZONES];
    int       count_ = 0;
};
