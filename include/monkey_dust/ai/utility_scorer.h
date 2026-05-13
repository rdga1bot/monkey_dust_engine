#pragma once
#include <monkey_dust/components/agent_state.h>
#include <cstdint>

// ── UtilityScorer ─────────────────────────────────────────────────────────────
// Echo-inspired goal utility scoring over MotivationType candidates.
// For each candidate motivation, a score [0..255] is computed from the
// agent's AgentState (gauges, awareness, alertness, timers).
// The highest-scoring motivation is selected.
//
// Usage:
//   MotivationType best = UtilityScorer::SelectMotivation(as, now_ms);
//   as.motivation = best;  // written by DirectorSystem or FlowGraph
//
// Scores are deterministic and fast (integer arithmetic, no branches > 6).
// Callers may add custom bonuses via the UtilityCandidateList before calling
// SelectMotivation.
//
// This replaces hard-coded if/else chains in DirectorSystem::ChooseMotivation.

static constexpr int US_MAX_CANDIDATES = static_cast<int>(MotivationType::COUNT);

struct UtilityCandidate {
    MotivationType motivation = MotivationType::Idle;
    uint8_t        score      = 0;
};

struct UtilityCandidateList {
    UtilityCandidate candidates[US_MAX_CANDIDATES] = {};
    int              count = 0;

    void Add(MotivationType m, uint8_t score) noexcept {
        if (count < US_MAX_CANDIDATES)
            candidates[count++] = {m, score};
    }
    // Bump the score of a motivation already in the list (no-op if absent).
    void Bias(MotivationType m, uint8_t bonus) noexcept {
        for (int i = 0; i < count; ++i)
            if (candidates[i].motivation == m) {
                int s = candidates[i].score + bonus;
                candidates[i].score = s > 255 ? 255u : (uint8_t)s;
                return;
            }
    }
};

class UtilityScorer {
public:
    // Compute default candidate scores from AgentState and return the best.
    // Tie-break: lower enum value wins (keeps current motivation stable).
    static MotivationType SelectMotivation(const AgentState& as,
                                           uint32_t now_ms) noexcept;

    // Low-level: fill a candidate list with baseline scores derived from AgentState.
    // Caller may add Bias() calls before invoking BestFrom().
    static void FillCandidates(const AgentState& as, uint32_t now_ms,
                                UtilityCandidateList& out) noexcept;

    // Return the highest-score motivation from a pre-built list.
    static MotivationType BestFrom(const UtilityCandidateList& list) noexcept;
};
