#pragma once
#include <cstdint>
#include <cstring>
#include <entt/entt.hpp>

static constexpr int    MAX_FACTIONS       = 64;
static constexpr int8_t HOSTILE_THRESHOLD  = -25;
static constexpr int8_t FRIENDLY_THRESHOLD = +25;

// ── BountyRecord ─────────────────────────────────────────────────────────────
// Per (faction, entity) outstanding criminal record.
// Kenshi RE: bounty expires only via prison sentence or faction nullification.

static constexpr int MAX_BOUNTIES = 128;

enum class CrimeFlags : uint8_t {
    None    = 0,
    Theft   = 1 << 0,
    Assault = 1 << 1,
    Murder  = 1 << 2,
    Trespass= 1 << 3,
};

struct BountyRecord {
    uint32_t        faction_id  = 0;    // offended faction
    entt::entity    target      = entt::null;
    uint32_t        amount      = 0;    // cats (Kenshi currency)
    uint8_t         crime_flags = 0;    // bitmask of CrimeFlags
    uint8_t         _pad[3]     = {};
};
static_assert(sizeof(BountyRecord) == 16, "BountyRecord must be 16 bytes");

struct FactionData {
    uint32_t id;
    char     name[32];
    char     slug[32];           // string id from md_world.json
    int8_t   relations[MAX_FACTIONS + 1];
    int8_t   default_relation;
    float    color_r, color_g, color_b;
    bool     loaded;
    // Kenshi Phase 2, Task 2.4 — faction simulation state
    int      population;              // current NPC count
    int      population_cap;          // max spawnable NPCs
    float    economy_wealth;          // abstract currency pool
    float    hostility_threshold;     // menace level [0..1] that triggers aggression
    float    tax_rate;                // fraction [0..1] taken from trade income
};

class FactionSystem {
public:
    static FactionSystem& Get() {
        static FactionSystem inst;
        return inst;
    }

    int LoadFromFile(const char* path);
    int LoadFromJson(const char* path);   // parse md_world.json factions array
    int LoadFromFcs (const char* path);   // parse gamedata.base / *.mod (Phase 4)

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

    // ── Bounty API ────────────────────────────────────────────────────────────
    // Add or accumulate a bounty for target in faction. Returns new total.
    uint32_t AddBounty(uint32_t faction_id, entt::entity target,
                       uint32_t amount, uint8_t crime_flags);

    // Get current bounty amount (0 if none).
    uint32_t GetBounty(uint32_t faction_id, entt::entity target) const;

    // Clear bounty (paid or served sentence).
    void ClearBounty(uint32_t faction_id, entt::entity target);

    // Iterate all active bounties (for guard AI).
    int BountyCount() const { return bounty_count_; }
    const BountyRecord& GetBountyByIndex(int i) const { return bounties_[i]; }

    float GetWealth(uint32_t faction_id) const {
        const FactionData* f = FindFaction(faction_id);
        return f ? f->economy_wealth : 0.f;
    }
    void ModWealth(uint32_t faction_id, float delta) {
        FactionData* f = FindFaction(faction_id);
        if (!f) return;
        f->economy_wealth += delta;
        if (f->economy_wealth < 0.f) f->economy_wealth = 0.f;
    }

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

    FactionData  factions_[MAX_FACTIONS];  // indexed 0..faction_count_-1
    int          faction_count_ = 0;

    BountyRecord bounties_[MAX_BOUNTIES];
    int          bounty_count_ = 0;
};
