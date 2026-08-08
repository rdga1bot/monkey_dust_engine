#include <monkey_dust/ai/behavior_tree.h>

uint16_t BehaviorTree::addConditionHasValidRouteToTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasValidRouteToTarget); return i;
}

// ── Batch 17 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionIsCharacterClass(CharacterClass cls) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionIsCharacterClass);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(cls));
    return i;
}
uint16_t BehaviorTree::addConditionIsInCover() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsInCover); return i;
}
uint16_t BehaviorTree::addConditionShouldUseCover() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionShouldUseCover); return i;
}
uint16_t BehaviorTree::addActionMoveToCover() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionMoveToCover); return i;
}
uint16_t BehaviorTree::addActionIdleInCover() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionIdleInCover); return i;
}

// ── Batch 18 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addBehaviourMoodSetCheck(BehaviourMoodSet mood_set) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::BehaviourMoodSetCheck);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(mood_set));
    return i;
}
uint16_t BehaviorTree::addSetBehaviourMoodSet(BehaviourMoodSet mood_set) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::SetBehaviourMoodSet);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(mood_set));
    return i;
}
uint16_t BehaviorTree::addViewconeTypeCheck(ViewconeType vtype) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ViewconeTypeCheck);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(vtype));
    return i;
}
uint16_t BehaviorTree::addSetViewconeType(ViewconeType vtype) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::SetViewconeType);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(vtype));
    return i;
}
uint16_t BehaviorTree::addSensoryTypeCheck(SensoryType stype) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::SensoryTypeCheck);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(stype));
    return i;
}
uint16_t BehaviorTree::addSetSensoryType(SensoryType stype) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::SetSensoryType);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(stype));
    return i;
}

// ── Batch 19 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionCurrentWeaponIsEquipped() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionCurrentWeaponIsEquipped); return i;
}
uint16_t BehaviorTree::addConditionCurrentWeaponNeedsReloading() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionCurrentWeaponNeedsReloading); return i;
}
uint16_t BehaviorTree::addConditionHasMeleeAttackAvailable() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasMeleeAttackAvailable); return i;
}
uint16_t BehaviorTree::addActionWeaponEquip() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionWeaponEquip); return i;
}
uint16_t BehaviorTree::addActionRangedShoot() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionRangedShoot); return i;
}
uint16_t BehaviorTree::addConditionHasObjective() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasObjective); return i;
}
uint16_t BehaviorTree::addConditionBehaviourMoodSetAbove(BehaviourMoodSet threshold) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionBehaviourMoodSetAbove);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(threshold));
    return i;
}

// ── Batch 20 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionSuccess() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionSuccess); return i;
}
uint16_t BehaviorTree::addActionFail() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionFail); return i;
}
uint16_t BehaviorTree::addDecoratorTimer(uint8_t timer_id, uint32_t duration_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::DecoratorTimer);
    m_nodes[i].data = (static_cast<uint32_t>(timer_id) << 24) | (duration_ms & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addActionIdle() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionIdle); return i;
}
uint16_t BehaviorTree::addConditionTargetIsWithinDistance(float max_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetIsWithinDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.0f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionHasAWeapon() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasAWeapon); return i;
}
uint16_t BehaviorTree::addConditionShouldProcessSuspiciousItem() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionShouldProcessSuspiciousItem); return i;
}

// ── Batch 21 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionMoveToTarget(LocomotionTargetSpeed speed) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionMoveToTarget);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(speed));
    return i;
}
uint16_t BehaviorTree::addActionMakeAggressive() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionMakeAggressive); return i;
}
uint16_t BehaviorTree::addConditionAllowedToAttackTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionAllowedToAttackTarget); return i;
}
uint16_t BehaviorTree::addActionDead() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionDead); return i;
}
uint16_t BehaviorTree::addActionMeleeAttack(uint8_t attack_type) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionMeleeAttack);
    m_nodes[i].data = static_cast<uint32_t>(attack_type & 0x03u);
    return i;
}
uint16_t BehaviorTree::addConditionIsCurrentCoverValid() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsCurrentCoverValid); return i;
}

// ── Batch 22 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionLastTimeSensed(uint8_t sense_idx, uint32_t max_elapsed_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionLastTimeSensed);
    m_nodes[i].data = (static_cast<uint32_t>(sense_idx) << 24) | (max_elapsed_ms & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addActionPerformRole(NpcRole role) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionPerformRole);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(role));
    return i;
}
uint16_t BehaviorTree::addConditionWasSenseThresholdLastIncreaseActivationAbove(
    uint8_t sense_idx, SenseThresholdQualifier q) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionWasSenseThresholdLastIncreaseActivationAbove);
    m_nodes[i].data = (static_cast<uint32_t>(sense_idx) << 8) | static_cast<uint32_t>(static_cast<uint8_t>(q));
    return i;
}
uint16_t BehaviorTree::addConditionTargetsWeaponHasAmmo() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionTargetsWeaponHasAmmo); return i;
}
uint16_t BehaviorTree::addConditionTargetsWeaponHasProperty(uint8_t weapon_type) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetsWeaponHasProperty);
    m_nodes[i].data = static_cast<uint32_t>(weapon_type);
    return i;
}
uint16_t BehaviorTree::addConditionHasSearchedMostRecentSensedPosition() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasSearchedMostRecentSensedPosition); return i;
}

// ── Batch 23 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionIdleTimeFacingTarget(uint32_t duration_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionIdleTimeFacingTarget);
    m_nodes[i].data = duration_ms;
    return i;
}
uint16_t BehaviorTree::addActionIdleTimeFacingTargetMostRecentSensedPosition(uint32_t duration_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionIdleTimeFacingTargetMostRecentSensedPosition);
    m_nodes[i].data = duration_ms;
    return i;
}
uint16_t BehaviorTree::addActionIdleTimeFacingSuspiciousItem(uint32_t duration_ms) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionIdleTimeFacingSuspiciousItem);
    m_nodes[i].data = duration_ms;
    return i;
}
uint16_t BehaviorTree::addActionRangedAim() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionRangedAim); return i;
}
uint16_t BehaviorTree::addActionSuspiciousItemReaction(SuspiciousItemReaction reaction) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionSuspiciousItemReaction);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(reaction));
    return i;
}
uint16_t BehaviorTree::addActionSuspectTargetResponse() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionSuspectTargetResponse); return i;
}

// ── Batch 24 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionRequestCover() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionRequestCover); return i;
}
uint16_t BehaviorTree::addConditionHasValidCoverToChangeTo() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasValidCoverToChangeTo); return i;
}

