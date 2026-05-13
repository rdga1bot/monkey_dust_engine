#include <monkey_dust/ai/utility_scorer.h>

// Score weights — tune per game design, no allocations.
static constexpr uint8_t kIdleBase      = 30;
static constexpr uint8_t kAttackBonus   = 80;  // when awareness == Aware
static constexpr uint8_t kFleeBonus     = 90;  // when stun gauge high
static constexpr uint8_t kSearchBonus   = 60;  // when awareness >= SearchingArea
static constexpr uint8_t kSuspBonus     = 50;  // when awareness == Suspicious
static constexpr uint8_t kThreatBonus   = 70;  // when alertness >= Combat

void UtilityScorer::FillCandidates(const AgentState& as, uint32_t /*now_ms*/,
                                   UtilityCandidateList& out) noexcept {
    out.count = 0;

    // Idle is always a valid candidate
    out.Add(MotivationType::Idle, kIdleBase);

    // Attack: driven by full awareness
    if (as.awareness == AwarenessState::Aware ||
        as.alertness == AlertnessState::Combat) {
        out.Add(MotivationType::Attack, kAttackBonus);
    }

    // Flee: driven by stun/retreat gauge being critically high
    if (as.gauges.get(GaugeType::StunDamage) >= 0.8f ||
        as.gauges.get(GaugeType::Retreat)    >= 0.8f) {
        out.Add(MotivationType::Flee, kFleeBonus);
    }

    // Search: threat spotted but not in combat lock-on
    if (as.awareness == AwarenessState::SearchingArea ||
        as.awareness == AwarenessState::Alert) {
        out.Add(MotivationType::Search, kSearchBonus);
    }

    // Suspicious: partial awareness (heard something)
    if (as.awareness == AwarenessState::Suspicious ||
        as.awareness == AwarenessState::Investigating) {
        out.Add(MotivationType::Suspicious, kSuspBonus);
    }

    // ThreatAware: elevated alertness without full combat lock-on
    if (as.alertness == AlertnessState::Alert) {
        out.Add(MotivationType::ThreatAware, kThreatBonus);
    }

    // Preserve current motivation with a small inertia bonus to avoid thrashing
    out.Bias(as.motivation, 10);
}

MotivationType UtilityScorer::BestFrom(const UtilityCandidateList& list) noexcept {
    if (list.count == 0) return MotivationType::Idle;
    MotivationType best = list.candidates[0].motivation;
    uint8_t        best_score = list.candidates[0].score;
    for (int i = 1; i < list.count; ++i) {
        if (list.candidates[i].score > best_score) {
            best_score = list.candidates[i].score;
            best       = list.candidates[i].motivation;
        }
    }
    return best;
}

MotivationType UtilityScorer::SelectMotivation(const AgentState& as,
                                               uint32_t now_ms) noexcept {
    UtilityCandidateList candidates;
    FillCandidates(as, now_ms, candidates);
    return BestFrom(candidates);
}
