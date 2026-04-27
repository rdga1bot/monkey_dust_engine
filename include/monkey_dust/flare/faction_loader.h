#pragma once
#include <cstdint>

namespace md::flare {

struct EnemyRegistry;  // forward declaration — avoids pulling in enemy_loader.h

constexpr int MAX_FLARE_FACTIONS = 64;

struct FlareFactionEntry {
    char    name[48];
    uint8_t id;    // 1-based sequential; 0 = invalid slot
    bool    valid;
};

struct FlareFactionRegistry {
    FlareFactionEntry entries[MAX_FLARE_FACTIONS];
    int               count;
};

// Derive faction registry from the primary category of each enemy definition.
// Each unique primary-category string gets a sequential id starting at 1.
// Call after LoadEnemies().
void BuildFactionsFromEnemies(const EnemyRegistry&  enemies,
                              FlareFactionRegistry& out);

// Return existing id or register a new one.  Returns 0 on overflow.
uint8_t GetOrAddFaction(FlareFactionRegistry& reg, const char* name);

// Find id by exact name match.  Returns 0 if not found.
uint8_t FindFactionId(const FlareFactionRegistry& reg, const char* name);

} // namespace md::flare
