#include <monkey_dust/world/world_simulation.h>
#include <monkey_dust/platform/md_log.h>

// 1 Hz world-state tick. Call with LOGIC_TICK_S (0.1f) each logic tick.
// Economy rules (per 1-second fire):
//   Passive gold generation: +1/tick baseline
//   Trade routes: from loses `volume` gold, to gains `volume/2`
//   Prosperity: rises if gold>0, falls if gold==0
//   Aggression: inversely tracks prosperity
//   Population: grows +1 every 10 sim ticks when prosperity>128 (P11)
//              decays -1 every 10 sim ticks when aggression>200   (P11)
//   Raids: attacker steals (defender.gold*strength/255) gold      (P12)
void WorldSimulation::Tick(float delta_s) noexcept {
    accum_s_ += delta_s;
    if (accum_s_ < 1.0f) return;
    accum_s_ -= 1.0f;

    ++tick_count_;

    // P12: Process pending raid events
    for (int r = 0; r < WS_MAX_RAIDS; ++r) {
        FactionRaidEvent& raid = pending_raids_[r];
        if (!raid.active) continue;
        FactionState* attacker = nullptr;
        FactionState* defender = nullptr;
        for (int i = 0; i < faction_count_; ++i) {
            if (factions_[i].faction_id == raid.attacker_faction) attacker = &factions_[i];
            if (factions_[i].faction_id == raid.defender_faction) defender = &factions_[i];
        }
        if (attacker && defender) {
            uint16_t loot = (uint16_t)(((uint32_t)defender->gold * raid.strength) / 255u);
            if (loot > defender->gold) loot = defender->gold;
            defender->gold -= loot;
            uint32_t gain = (uint32_t)attacker->gold + loot;
            attacker->gold = (uint16_t)(gain < 0xFFFFu ? gain : 0xFFFFu);
        }
        raid.active = 0;  // consume
    }

    // Apply trade routes
    for (int r = 0; r < route_count_; ++r) {
        const TradeRoute& rt = routes_[r];
        if (!rt.active || rt.volume == 0) continue;
        FactionState* src = nullptr;
        FactionState* dst = nullptr;
        for (int i = 0; i < faction_count_; ++i) {
            if (factions_[i].faction_id == rt.from_faction) src = &factions_[i];
            if (factions_[i].faction_id == rt.to_faction)   dst = &factions_[i];
        }
        if (src && src->gold >= rt.volume) {
            src->gold = (uint16_t)(src->gold - rt.volume);
            if (dst) {
                uint16_t gain = (uint16_t)(rt.volume / 2u);
                dst->gold = (uint16_t)(dst->gold + gain < 0xFFFF ? dst->gold + gain : 0xFFFF);
            }
        }
    }

    // Update faction economy
    for (int i = 0; i < faction_count_; ++i) {
        FactionState& f = factions_[i];
        if (f.gold < 0xFFFF) ++f.gold;  // passive income
        if (f.gold > 0 && f.prosperity < 255) ++f.prosperity;
        else if (f.gold == 0 && f.prosperity > 0) --f.prosperity;
        if (f.prosperity < 32u && f.aggression < 255) ++f.aggression;
        else if (f.prosperity > 32u && f.aggression > 0)  --f.aggression;
        // P11: population dynamics
        if ((tick_count_ % 10u) == 0u) {
            if (f.prosperity > 128u && f.population < 255) ++f.population;
            if (f.aggression > 200u && f.population > 0)  --f.population;
        }
    }
}

// P12: Queue a one-shot raid event
void WorldSimulation::QueueRaid(uint8_t attacker, uint8_t defender,
                                uint8_t strength) noexcept {
    for (int i = 0; i < WS_MAX_RAIDS; ++i) {
        if (!pending_raids_[i].active) {
            pending_raids_[i] = {attacker, defender, strength, 1};
            return;
        }
    }
    MD_LOG(MD_LOG_WARNING, "WorldSimulation: raid queue full");
}
