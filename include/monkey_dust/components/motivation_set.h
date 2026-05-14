#pragma once
#include <cstdint>
#include <monkey_dust/components/agent_state.h>  // MotivationType

// MotivationSet — CATHODE ConditionHasMotivation pattern:
// Multiple simultaneous motivations, each with a uint8 priority (0=lowest).
// BestFrom() returns the active motivation with the highest priority.
// Replaces the single MotivationType field in AgentState for entities
// that need complex multi-goal behaviour (e.g. the Alien).
//
// MAX_MOTIVATIONS=4; fits in 20 bytes with header.

static constexpr uint8_t MAX_MOTIVATIONS = 4;

struct MotivationEntry {
    MotivationType type;
    uint8_t        priority;  // higher = more important
};
static_assert(sizeof(MotivationEntry) == 2);

struct MotivationSetComponent {
    MotivationEntry entries[MAX_MOTIVATIONS];
    uint8_t         count;
    uint8_t         _pad[3];
};
static_assert(sizeof(MotivationSetComponent) == 12);

// Add motivation with priority. Replaces existing entry with same type.
// Returns false if full and type not already present.
inline bool motivation_set_add(MotivationSetComponent& ms,
                                MotivationType type, uint8_t priority) noexcept {
    for (uint8_t i = 0; i < ms.count; ++i) {
        if (ms.entries[i].type == type) {
            ms.entries[i].priority = priority;
            return true;
        }
    }
    if (ms.count >= MAX_MOTIVATIONS) return false;
    ms.entries[ms.count++] = { type, priority };
    return true;
}

inline void motivation_set_remove(MotivationSetComponent& ms,
                                   MotivationType type) noexcept {
    uint8_t w = 0;
    for (uint8_t r = 0; r < ms.count; ++r)
        if (ms.entries[r].type != type) ms.entries[w++] = ms.entries[r];
    ms.count = w;
}

inline bool motivation_set_has(const MotivationSetComponent& ms,
                                 MotivationType type) noexcept {
    for (uint8_t i = 0; i < ms.count; ++i)
        if (ms.entries[i].type == type) return true;
    return false;
}

// Returns motivation with the highest priority; None if empty.
inline MotivationType motivation_set_best(const MotivationSetComponent& ms) noexcept {
    if (!ms.count) return MotivationType::None;
    uint8_t best_pri = 0; uint8_t best_i = 0;
    for (uint8_t i = 0; i < ms.count; ++i) {
        if (ms.entries[i].priority >= best_pri) {
            best_pri = ms.entries[i].priority; best_i = i;
        }
    }
    return ms.entries[best_i].type;
}
