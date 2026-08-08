#include <monkey_dust/ai/behavior_tree.h>

uint16_t BehaviorTree::addConditionHasAnySenseBeenAboveWithinTime(SenseThresholdQualifier q,
                                                                   uint32_t time_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionHasAnySenseBeenAboveWithinTime);
    m_nodes[i].data = (static_cast<uint32_t>(q) << 24) | (time_ms & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addConditionIsCorpseTrap() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsCorpseTrap); return i;
}
uint16_t BehaviorTree::addConditionIsGaugeAmountBelow(GaugeType gauge, float threshold) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionIsGaugeAmountBelow);
    uint32_t t = static_cast<uint32_t>(threshold * 1000.f + 0.5f);
    m_nodes[i].data = (static_cast<uint32_t>(gauge) << 24) | (t & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addConditionLastTimeWasAbleToShootTarget(uint32_t max_elapsed_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionLastTimeWasAbleToShootTarget);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addConditionRequiresPrimaryDamageControlResponse(uint8_t threshold_pct) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionRequiresPrimaryDamageControlResponse);
    m_nodes[i].data = threshold_pct;
    return i;
}
uint16_t BehaviorTree::addConditionTargetIsPlayer() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionTargetIsPlayer); return i;
}
uint16_t BehaviorTree::addConditionTargetIsWithinDistanceUnobscured(float max_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetIsWithinDistanceUnobscured);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionTargetLogicCharacterFlags(uint8_t bit_idx, bool check_set) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetLogicCharacterFlags);
    m_nodes[i].data = (static_cast<uint32_t>(bit_idx) << 8) | (check_set ? 1u : 0u);
    return i;
}

// ── Batch 34 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionForceSearch() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionForceSearch); return i;
}
uint16_t BehaviorTree::addActionResetSearchJobs() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionResetSearchJobs); return i;
}
uint16_t BehaviorTree::addActionSetFrameFlag(uint8_t bit_idx) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionSetFrameFlag);
    m_nodes[i].data = bit_idx;
    return i;
}
uint16_t BehaviorTree::addActionSetGaugeAmount(GaugeType gauge, float value) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionSetGaugeAmount);
    uint32_t v = static_cast<uint32_t>(value * 1000.f + 0.5f);
    if (v > 0xFFFFu) v = 0xFFFFu;
    m_nodes[i].data = (static_cast<uint32_t>(gauge) << 16) | v;
    return i;
}
uint16_t BehaviorTree::addConditionHasSenseActivationBeenAbove(uint8_t sense_idx,
                                                                SenseThresholdQualifier q) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionHasSenseActivationBeenAbove);
    m_nodes[i].data = (static_cast<uint32_t>(sense_idx) << 8) | static_cast<uint32_t>(q);
    return i;
}
uint16_t BehaviorTree::addConditionIsCoverTooClose(float min_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionIsCoverTooClose);
    m_nodes[i].data = static_cast<uint32_t>(min_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionIsInCombatArea() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsInCombatArea); return i;
}
uint16_t BehaviorTree::addConditionMostRecentSenseActivationHasBeenAbove(SenseThresholdQualifier q) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionMostRecentSenseActivationHasBeenAbove);
    m_nodes[i].data = static_cast<uint32_t>(q);
    return i;
}
uint16_t BehaviorTree::addConditionNeedsToGetOutOfTheWay() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionNeedsToGetOutOfTheWay); return i;
}
uint16_t BehaviorTree::addConditionObjectiveIsInCombatArea() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionObjectiveIsInCombatArea); return i;
}
uint16_t BehaviorTree::addConditionObjectiveIsWithinDistance(float max_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionObjectiveIsWithinDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionShouldFollow() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionShouldFollow); return i;
}
uint16_t BehaviorTree::addConditionTargetIsInCombatArea() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionTargetIsInCombatArea); return i;
}
uint16_t BehaviorTree::addConditionTargetIsWithinAggroRadius(float radius_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetIsWithinAggroRadius);
    m_nodes[i].data = static_cast<uint32_t>(radius_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionTargetNearestStandPointIsWithinDistance(float max_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetNearestStandPointIsWithinDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addDecoratorSetSenseSet(uint8_t sense_set_id) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::DecoratorSetSenseSet);
    m_nodes[i].data = sense_set_id;
    return i;
}
uint16_t BehaviorTree::addDecoratorSuspiciousItemInProgress() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::DecoratorSuspiciousItemInProgress); return i;
}
uint16_t BehaviorTree::addSelectorLinear() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::SelectorLinear); return i;
}
uint16_t BehaviorTree::addSequenceLinear() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::SequenceLinear); return i;
}

// ── Batch 33 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionAbortMeleeAttack() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionAbortMeleeAttack); return i;
}
uint16_t BehaviorTree::addActionGetOutOfTheWay() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionGetOutOfTheWay); return i;
}
uint16_t BehaviorTree::addActionHitTargetAndRun() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionHitTargetAndRun); return i;
}
uint16_t BehaviorTree::addActionMoveInDirection(uint8_t direction) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionMoveInDirection);
    m_nodes[i].data = direction & 0x3u;
    return i;
}
uint16_t BehaviorTree::addActionSuspend() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionSuspend); return i;
}
uint16_t BehaviorTree::addActionTakeStep() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionTakeStep); return i;
}
uint16_t BehaviorTree::addActionThreatAware() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionThreatAware); return i;
}
uint16_t BehaviorTree::addActionThreatEscalation() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionThreatEscalation); return i;
}
uint16_t BehaviorTree::addConditionCanShootNow() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionCanShootNow); return i;
}
uint16_t BehaviorTree::addConditionCheckHealthState(uint8_t threshold_pct) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionCheckHealthState);
    m_nodes[i].data = threshold_pct;
    return i;
}
uint16_t BehaviorTree::addConditionHasGroupAwarenessState(AwarenessState state) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionHasGroupAwarenessState);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}
uint16_t BehaviorTree::addConditionHasMeleeBlockAvailable() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasMeleeBlockAvailable); return i;
}
uint16_t BehaviorTree::addConditionHasMeleeCounterAttackAvailable() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasMeleeCounterAttackAvailable); return i;
}
uint16_t BehaviorTree::addConditionLastTimeTargetShotAtMe(uint32_t max_elapsed_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionLastTimeTargetShotAtMe);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addConditionTargetIsInWeaponRange() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionTargetIsInWeaponRange); return i;
}
uint16_t BehaviorTree::addConditionTargetIsTargetingMe() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionTargetIsTargetingMe); return i;
}
uint16_t BehaviorTree::addConditionTargetIsUsingMeleeAttack() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionTargetIsUsingMeleeAttack); return i;
}
uint16_t BehaviorTree::addDecoratorLoop(uint32_t count) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::DecoratorLoop);
    m_nodes[i].data = count;
    return i;
}

// ── Batch 32 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionHasToken(uint32_t token_id) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionHasToken);
    m_nodes[i].data = token_id;
    return i;
}
uint16_t BehaviorTree::addActionReleaseToken(uint32_t token_id) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionReleaseToken);
    m_nodes[i].data = token_id;
    return i;
}

