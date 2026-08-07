#include <monkey_dust/ai/behavior_tree.h>

// ── Batch 25 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionHasScript() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasScript); return i;
}
uint16_t BehaviorTree::addActionScript(uint32_t script_name_hash) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionScript);
    m_nodes[i].data = script_name_hash;
    return i;
}

// ── Batch 26 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionLastTimeSearchedWithinTime(uint32_t max_elapsed_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionLastTimeSearchedWithinTime);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addConditionAllowedToSearch() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionAllowedToSearch); return i;
}
uint16_t BehaviorTree::addConditionHasDoneSuspectResponseMoveTo() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasDoneSuspectResponseMoveTo); return i;
}
uint16_t BehaviorTree::addConditionHasDoneSuspectResponseWithinTime(uint32_t max_elapsed_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionHasDoneSuspectResponseWithinTime);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addConditionHasKilltrap() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasKilltrap); return i;
}
uint16_t BehaviorTree::addConditionHasMeleeAttackAvailableOrIsAttacking() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasMeleeAttackAvailableOrIsAttacking); return i;
}
uint16_t BehaviorTree::addConditionIsBranchActive(uint32_t branch_name_hash) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionIsBranchActive);
    m_nodes[i].data = branch_name_hash;
    return i;
}
uint16_t BehaviorTree::addConditionIsRequestingCover() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsRequestingCover); return i;
}
uint16_t BehaviorTree::addConditionAllowedToPursueTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionAllowedToPursueTarget); return i;
}

// ── Batch 27 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionBreakout() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionBreakout); return i;
}
uint16_t BehaviorTree::addActionMoveToMostRecentSensedPosition() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionMoveToMostRecentSensedPosition); return i;
}
uint16_t BehaviorTree::addActionMoveToNearestStandingPointToTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionMoveToNearestStandingPointToTarget); return i;
}
uint16_t BehaviorTree::addActionMoveToAttackTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionMoveToAttackTarget); return i;
}
uint16_t BehaviorTree::addActionChangeCover() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionChangeCover); return i;
}

// ── Batch 28 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionIsInTargetsWeaponRange(float max_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionIsInTargetsWeaponRange);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionIsCoverExposed() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsCoverExposed); return i;
}
uint16_t BehaviorTree::addConditionHasLostTarget(uint32_t max_elapsed_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionHasLostTarget);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addActionUpdateLastKnownPosition() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionUpdateLastKnownPosition); return i;
}
uint16_t BehaviorTree::addActionRequestInvestigate() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionRequestInvestigate); return i;
}
uint16_t BehaviorTree::addActionMarkTargetLost() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionMarkTargetLost); return i;
}
uint16_t BehaviorTree::addActionForceRetreat() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionForceRetreat); return i;
}
uint16_t BehaviorTree::addActionHoldPosition() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionHoldPosition); return i;
}

// ── Batch 29 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionNpcDevelopmentStageAbove(NpcDevelopmentStage stage) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionNpcDevelopmentStageAbove);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint16_t>(stage));
    return i;
}
uint16_t BehaviorTree::addConditionNpcHasAbility(uint16_t ability_mask) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionNpcHasAbility);
    m_nodes[i].data = static_cast<uint32_t>(ability_mask);
    return i;
}
uint16_t BehaviorTree::addConditionIsHostileToPlayer() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsHostileToPlayer); return i;
}
uint16_t BehaviorTree::addActionPerformAmbush() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionPerformAmbush); return i;
}
uint16_t BehaviorTree::addActionStartSearch() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionStartSearch); return i;
}
uint16_t BehaviorTree::addActionCallForHelp() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionCallForHelp); return i;
}
uint16_t BehaviorTree::addActionTauntTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionTauntTarget); return i;
}
uint16_t BehaviorTree::addActionSurrenderSelf() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionSurrenderSelf); return i;
}

// ── Batch 30 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionEventCountAbove(uint8_t min_count) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionEventCountAbove);
    m_nodes[i].data = static_cast<uint32_t>(min_count);
    return i;
}
uint16_t BehaviorTree::addConditionSpatialMemoryCountAbove(uint8_t min_count) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionSpatialMemoryCountAbove);
    m_nodes[i].data = static_cast<uint32_t>(min_count);
    return i;
}
uint16_t BehaviorTree::addConditionMotivationTicksAbove(uint8_t min_ticks) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionMotivationTicksAbove);
    m_nodes[i].data = static_cast<uint32_t>(min_ticks);
    return i;
}
uint16_t BehaviorTree::addConditionHasVisualHistory() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasVisualHistory); return i;
}
uint16_t BehaviorTree::addActionPursueTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionPursueTarget); return i;
}
uint16_t BehaviorTree::addActionCircleTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionCircleTarget); return i;
}
uint16_t BehaviorTree::addActionBackOff() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionBackOff); return i;
}
uint16_t BehaviorTree::addActionCrouchMove() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionCrouchMove); return i;
}
uint16_t BehaviorTree::addActionVault() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionVault); return i;
}

// ── Batch 31 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionHasVentCloseToAlien(float radius_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionHasVentCloseToAlien);
    m_nodes[i].data = static_cast<uint32_t>(radius_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionHasFlankedVentCloseToPlayer(float radius_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionHasFlankedVentCloseToPlayer);
    m_nodes[i].data = static_cast<uint32_t>(radius_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionTargetIsOnlyAccessibleCrouching() {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetIsOnlyAccessibleCrouching);
    return i;
}
uint16_t BehaviorTree::addConditionAngleNPCToTargetsAimLessThan(float angle_deg) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionAngleNPCToTargetsAimLessThan);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(angle_deg + 0.5f));
    return i;
}

// ── Batch 35 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionApplyPrimaryDamageControlResponse() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionApplyPrimaryDamageControlResponse); return i;
}
uint16_t BehaviorTree::addActionBrokenCover() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionBrokenCover); return i;
}
uint16_t BehaviorTree::addActionMoveToObjective() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionMoveToObjective); return i;
}
uint16_t BehaviorTree::addActionSetLogicCharacterFlags(uint8_t bit_idx, bool do_set) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionSetLogicCharacterFlags);
    m_nodes[i].data = (static_cast<uint32_t>(bit_idx) << 8) | (do_set ? 1u : 0u);
    return i;
}
uint16_t BehaviorTree::addConditionAllowedToDoSuspiciousWarning() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionAllowedToDoSuspiciousWarning); return i;
}
uint16_t BehaviorTree::addConditionCanTakeStep() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionCanTakeStep); return i;
}
uint16_t BehaviorTree::addConditionGameIsDifficulty() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionGameIsDifficulty); return i;
}
