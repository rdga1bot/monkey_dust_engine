#pragma once
#include <cstdint>

// PrisonerComponent — E-1: slave/prisoner social state.
// Kenshi RE: slavestate, slave_ai_goals, Squad Campaign pattern.
// Attach to NPC entities captured by a faction.

enum class SlaveState : uint8_t {
    Free     = 0,  // not enslaved
    Prisoner = 1,  // captured but not yet assigned as slave
    Slave    = 2,  // assigned to work for captor faction
    Escaped  = 3,  // escaped; captor faction will pursue
};

// BT goal type for slave AI (read by BT condition/action leaves).
enum class SlaveGoalType : uint8_t {
    Follow  = 0,  // follow captor NPC or stay in assigned zone
    Work    = 1,  // perform labour task (ProductionChain building)
    Escape  = 2,  // attempt to flee toward map edge
    Revolt  = 3,  // attack captor when conditions met
};

struct PrisonerComponent {
    uint32_t  captor_faction_id = 0;              // 0 = no captor
    SlaveState slave_state      = SlaveState::Free;
    uint8_t   days_enslaved     = 0;              // saturates at 255
    uint8_t   escape_attempts   = 0;              // each attempt adds fatigue penalty
    uint8_t   revolt_counter    = 0;              // increments when morale < threshold
};
static_assert(sizeof(PrisonerComponent) == 8, "PrisonerComponent must be 8 bytes");

// Kenshi: escape chance per tick = Athletics skill × 0.01.
// Returns true if escape attempt succeeds this tick (caller provides skill + rng_0_1).
inline bool TryEscapeRoll(uint8_t athletics_skill, float rng_0_1) noexcept {
    float chance = (float)athletics_skill * 0.01f;
    return rng_0_1 < chance;
}
