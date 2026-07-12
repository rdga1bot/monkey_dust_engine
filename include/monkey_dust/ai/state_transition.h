#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <cstdint>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/platform/md_log.h>

// StateTransitionTable — MD EntityState 40+ methods pattern:
// Maps (required_flags_mask, forbidden_flags_mask) → action callback.
// Transitions are evaluated in priority order; first matching rule fires.
// MAX_RULES=16; no heap; raw function pointers.
//
// Example: on Frozen+Spawn set → fire "on_frozen_spawn" handler.

using StateTransitionFn = void (*)(MdEntity, uint32_t new_state);

struct StateTransitionRule {
    uint32_t          required;    // flags that MUST be set
    uint32_t          forbidden;   // flags that MUST NOT be set
    StateTransitionFn action;
    uint8_t           priority;    // lower = fires first
    uint8_t           _pad[7];
};
static_assert(sizeof(StateTransitionRule) == 24);

class StateTransitionTable {
public:
    static constexpr uint8_t MAX_RULES = 16;

    static StateTransitionTable& Get() noexcept {
        static StateTransitionTable inst;
        return inst;
    }

    bool AddRule(uint32_t required, uint32_t forbidden,
                 StateTransitionFn fn, uint8_t priority = 128) noexcept {
        if (count_ >= MAX_RULES) {
            MD_LOG(MD_LOG_WARNING, "StateTransitionTable: full (%d rules)", MAX_RULES);
            return false;
        }
        rules_[count_++] = { required, forbidden, fn, priority };
        sort();
        return true;
    }

    void Clear() noexcept { count_ = 0; }

    // Evaluate all matching rules against new_state; fire each in priority order.
    void Evaluate(MdEntity e, uint32_t new_state) const noexcept {
        for (uint8_t i = 0; i < count_; ++i) {
            const auto& r = rules_[i];
            bool req_ok  = (new_state & r.required)  == r.required;
            bool forb_ok = (new_state & r.forbidden)  == 0u;
            if (req_ok && forb_ok) r.action(e, new_state);
        }
    }

    uint8_t count() const noexcept { return count_; }

private:
    StateTransitionRule rules_[MAX_RULES] = {};
    uint8_t             count_ = 0;

    void sort() noexcept {
        // insertion sort (MAX=16, so O(N²) is fine)
        for (uint8_t i = 1; i < count_; ++i) {
            StateTransitionRule key = rules_[i];
            int8_t j = (int8_t)i - 1;
            while (j >= 0 && rules_[j].priority > key.priority) {
                rules_[j + 1] = rules_[j]; --j;
            }
            rules_[j + 1] = key;
        }
    }
};
