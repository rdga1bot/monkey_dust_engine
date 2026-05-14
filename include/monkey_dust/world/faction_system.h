#pragma once
#include <cstdint>
#include <cstring>

static constexpr int    MAX_FACTIONS       = 16;
static constexpr int8_t HOSTILE_THRESHOLD  = -25;
static constexpr int8_t FRIENDLY_THRESHOLD = +25;

struct FactionData {
    uint32_t id;
    char     name[32];
    int8_t   relations[MAX_FACTIONS + 1];
    int8_t   default_relation;
    bool     loaded;
};

class FactionSystem {
public:
    static FactionSystem& Get() {
        static FactionSystem inst;
        return inst;
    }

    int LoadFromFile(const char* path);

    int8_t GetRelation(uint32_t from, uint32_t to) const;

    bool IsEnemy(uint32_t a, uint32_t b) const {
        return GetRelation(a, b) < HOSTILE_THRESHOLD;
    }
    bool IsNeutral(uint32_t a, uint32_t b) const {
        int8_t r = GetRelation(a, b);
        return r >= HOSTILE_THRESHOLD && r <= FRIENDLY_THRESHOLD;
    }
    bool IsFriendly(uint32_t a, uint32_t b) const {
        return GetRelation(a, b) > FRIENDLY_THRESHOLD;
    }

    const char* GetName(uint32_t id) const;

    void SetRelation(uint32_t from, uint32_t to, int8_t value);
    void ModRelation(uint32_t from, uint32_t to, int8_t delta);

    int FactionCount() const { return faction_count_; }

    int8_t GetRelationByIndex(int a, int b) const {
        if (a < 0 || a >= faction_count_) return 0;
        if (b < 0 || b >= faction_count_) return 0;
        return factions_[a].relations[factions_[b].id];
    }
    const char* GetNameByIndex(int idx) const {
        if (idx < 0 || idx >= faction_count_) return "?";
        return factions_[idx].name;
    }

private:
    FactionSystem() {
        memset(factions_, 0, sizeof(factions_));
        faction_count_ = 0;
    }

    FactionData* FindFaction(uint32_t id);
    const FactionData* FindFaction(uint32_t id) const;

    FactionData factions_[MAX_FACTIONS];
    int         faction_count_ = 0;
};
