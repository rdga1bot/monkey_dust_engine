#include <monkey_dust/world/world_simulation.h>

// 1 Hz world-state tick. Call with LOGIC_TICK_S (0.1f) each logic tick.
// Economy rules (per 1-second fire):
//   Passive gold generation: +1/tick baseline
//   Trade routes: from loses `volume` gold, to gains `volume/2`
//   Prosperity: rises if gold>0, falls if gold==0
//   Aggression: inversely tracks prosperity
//   Population: grows +1 every 10 sim ticks when prosperity>128
void WorldSimulation::Tick(float delta_s) noexcept {
    accum_s_ += delta_s;
    if (accum_s_ < 1.0f) return;
    accum_s_ -= 1.0f;

    static uint32_t tick_count = 0;
    ++tick_count;

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
        if ((tick_count % 10u) == 0u && f.prosperity > 128u && f.population < 255)
            ++f.population;
    }
}
