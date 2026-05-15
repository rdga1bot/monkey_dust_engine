#pragma once
#include <cstdint>
#include <cstring>

// ── WorldSimulation ───────────────────────────────────────────────────────────
// Echo-inspired 1 Hz world-state simulation: faction economy + trade routes.
// Runs at 1 tick/second, decoupled from the 10 TPS logic tick.
//
// FactionState[MAX_FACTIONS]: faction-level economy metrics updated each sim tick.
// TradeRoute[MAX_ROUTES]:     directed trade between two factions; active flag.
// FactionRaidEvent[MAX_RAIDS]:one-shot raid events processed on next 1 Hz tick.
//
// Integration: call Tick(now_s) from logic_tick.cpp (~every 10 logic ticks = 1s).
// Access: WorldSimulation::Get() singleton; all arrays are fixed BSS.

static constexpr int WS_MAX_FACTIONS = 8;
static constexpr int WS_MAX_ROUTES   = 32;
static constexpr int WS_MAX_RAIDS    = 4;

struct FactionState {
    uint8_t  faction_id    = 0;
    uint8_t  aggression    = 50;  // 0=passive, 255=always hostile
    uint8_t  prosperity    = 50;  // 0=starving, 255=wealthy
    uint8_t  population    = 1;   // 0=extinct; increments slowly
    uint16_t gold          = 0;
    uint16_t _pad          = 0;
};
static_assert(sizeof(FactionState) == 8, "");

struct TradeRoute {
    uint8_t from_faction = 0;
    uint8_t to_faction   = 0;
    uint8_t volume       = 0;   // trade units/tick (0 = route disabled)
    uint8_t active       = 0;   // 0=closed, 1=open
};
static_assert(sizeof(TradeRoute) == 4, "");

// ── Batch 13 P12: FactionRaidEvent ────────────────────────────────────────────
// One-shot raid processed on the next 1 Hz WorldSimulation tick.
// Attacker steals (defender.gold * strength / 255) gold from defender.
// Slot is freed (active=0) after processing.
struct FactionRaidEvent {
    uint8_t attacker_faction = 0;
    uint8_t defender_faction = 0;
    uint8_t strength         = 0;  // raid intensity 0–255; proportional gold taken
    uint8_t active           = 0;  // 0=free slot, 1=pending
};
static_assert(sizeof(FactionRaidEvent) == 4, "");

class WorldSimulation {
public:
    static WorldSimulation& Get() {
        static WorldSimulation inst;
        return inst;
    }

    // Call from logic_tick every logic-tick with delta_s (0.1f).
    // Internally accumulates; fires economy update at 1 Hz.
    void Tick(float delta_s) noexcept;

    // Accessors used by Lua / Director
    FactionState& GetFaction(uint8_t id) noexcept {
        for (int i = 0; i < faction_count_; ++i)
            if (factions_[i].faction_id == id) return factions_[i];
        return factions_[0];
    }

    void RegisterFaction(uint8_t id, uint8_t aggression = 50,
                         uint8_t prosperity = 50) noexcept {
        if (faction_count_ >= WS_MAX_FACTIONS) return;
        factions_[faction_count_++] = {id, aggression, prosperity, 1, 0, 0};
    }

    void OpenTradeRoute(uint8_t from, uint8_t to, uint8_t volume = 10) noexcept {
        if (route_count_ >= WS_MAX_ROUTES) return;
        routes_[route_count_++] = {from, to, volume, 1};
    }

    void CloseTradeRoute(uint8_t from, uint8_t to) noexcept {
        for (int i = 0; i < route_count_; ++i)
            if (routes_[i].from_faction == from && routes_[i].to_faction == to)
                routes_[i].active = 0;
    }

    // P12: Queue a one-shot raid event. Processed on next 1 Hz tick.
    // Drops silently if all WS_MAX_RAIDS slots are occupied.
    void QueueRaid(uint8_t attacker, uint8_t defender, uint8_t strength) noexcept;

    // Reset all state — use in tests between test cases.
    void Reset() noexcept {
        faction_count_ = route_count_ = 0;
        accum_s_       = 0.f;
        tick_count_    = 0;
        memset(factions_,     0, sizeof(factions_));
        memset(routes_,       0, sizeof(routes_));
        memset(pending_raids_, 0, sizeof(pending_raids_));
    }

    FactionState     factions_    [WS_MAX_FACTIONS] = {};
    TradeRoute       routes_      [WS_MAX_ROUTES]   = {};
    FactionRaidEvent pending_raids_[WS_MAX_RAIDS]   = {};
    int              faction_count_ = 0;
    int              route_count_   = 0;

private:
    WorldSimulation() = default;
    float    accum_s_    = 0.f;
    uint32_t tick_count_ = 0;
};
