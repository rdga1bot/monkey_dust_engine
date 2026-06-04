#include <monkey_dust/ai/behavior_tree.h>

// ── Batch 2 factories ────────────────────────────────────────────────────────

uint16_t BehaviorTree::addConditionIsDead() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsDead); return i;
}
uint16_t BehaviorTree::addConditionIsInVent(uint8_t char_type) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionIsInVent);
    m_nodes[i].data = char_type & 0x3u;
    return i;
}
uint16_t BehaviorTree::addConditionCanBreakout() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionCanBreakout); return i;
}
uint16_t BehaviorTree::addConditionIsBackstage() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsBackstage); return i;
}
uint16_t BehaviorTree::addConditionIsPartOfNPCGroup() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsPartOfNPCGroup); return i;
}
uint16_t BehaviorTree::addConditionAnotherAlienIsAttacking() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionAnotherAlienIsAttacking); return i;
}
uint16_t BehaviorTree::addConditionHasSearchedPos() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasSearchedPos); return i;
}
uint16_t BehaviorTree::addConditionHasDoneSuspectMoveTo() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasDoneSuspectMoveTo); return i;
}
uint16_t BehaviorTree::addActionSwitchToNextTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionSwitchToNextTarget); return i;
}
uint16_t BehaviorTree::addActionDoneSystematicSearch() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionDoneSystematicSearch); return i;
}

// ── Batch 4: EventOrder ───────────────────────────────────────────────────────

uint16_t BehaviorTree::addConditionEventAOccuredAfterB(EventType a, EventType b) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionEventAOccuredAfterB);
    m_nodes[i].data = (static_cast<uint32_t>(a) << 8) | static_cast<uint32_t>(b);
    return i;
}

// ── Batch 5: Squad extensions ─────────────────────────────────────────────────

uint16_t BehaviorTree::addConditionSquadDoingEscalation() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionSquadDoingEscalation); return i;
}
uint16_t BehaviorTree::addConditionSquadDoingSuspiciousWarning() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionSquadDoingSuspiciousWarning); return i;
}
uint16_t BehaviorTree::addDecoratorSquadSearch() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::DecoratorSquadSearch); return i;
}

// ── Batch 6: SuspiciousItem Group system ──────────────────────────────────────

uint16_t BehaviorTree::addConditionSuspiciousItemShouldDoStage(SuspiciousItemStage stage) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemShouldDoStage);
    m_nodes[i].data = static_cast<uint32_t>(stage);
    return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemIsWithinDistance(float max_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemIsWithinDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 100.f + 0.5f); // cm
    return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemFirstGroupMember() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemFirstGroupMember); return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemGroupAllowedToProgress() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemGroupAllowedToProgress); return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemGroupMembersRoutingTo() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemGroupMembersRoutingTo); return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemWaitForGroupRouting() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemWaitForGroupRouting); return i;
}

// ── Batch 7: SuspiciousItemReaction / AmbushType / NoiseType ─────────────────
uint16_t BehaviorTree::addSIReactionCheck(SuspiciousItemReaction reaction) {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::SIReactionCheck);
    m_nodes[i].data = static_cast<uint32_t>(reaction); return i;
}
uint16_t BehaviorTree::addSetSIReaction(SuspiciousItemReaction reaction) {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::SetSIReaction);
    m_nodes[i].data = static_cast<uint32_t>(reaction); return i;
}
uint16_t BehaviorTree::addAmbushTypeCheck(AmbushType type) {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::AmbushTypeCheck);
    m_nodes[i].data = static_cast<uint32_t>(type); return i;
}
uint16_t BehaviorTree::addSetAmbushType(AmbushType type) {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::SetAmbushType);
    m_nodes[i].data = static_cast<uint32_t>(type); return i;
}
uint16_t BehaviorTree::addNoiseTypeCheck(NoiseType type) {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::NoiseTypeCheck);
    m_nodes[i].data = static_cast<uint32_t>(type); return i;
}
uint16_t BehaviorTree::addSetNoiseType(NoiseType type) {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::SetNoiseType);
    m_nodes[i].data = static_cast<uint32_t>(type); return i;
}

// ── Batch 8: SI lifecycle ─────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionSuspiciousItemValid() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemValid); return i;
}
uint16_t BehaviorTree::addActionConsumeSuspiciousItem() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionConsumeSuspiciousItem); return i;
}
uint16_t BehaviorTree::addActionForceMoveToSI() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionForceMoveToSI); return i;
}

// ── Batch 9 (Step 5): Inter-NPC relationship nodes ────────────────────────────
uint16_t BehaviorTree::addRelationshipTrustCheck(uint8_t threshold, uint8_t mode) {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::RelationshipTrustCheck);
    m_nodes[i].data = (static_cast<uint32_t>(threshold) << 8) | (mode & 0x1u); return i;
}
uint16_t BehaviorTree::addRelationshipFearCheck(uint8_t threshold, uint8_t mode) {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::RelationshipFearCheck);
    m_nodes[i].data = (static_cast<uint32_t>(threshold) << 8) | (mode & 0x1u); return i;
}

// ── Batch 10: MD_z.md BEHAVIOR XML patterns ──────────────────────────────────

uint16_t BehaviorTree::addConditionIsAnySenseActivationAbove(SenseThresholdQualifier q) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionIsAnySenseActivationAbove);
    m_nodes[i].data = static_cast<uint32_t>(q);
    return i;
}

uint16_t BehaviorTree::addActionMoveThroughTarget(LocomotionTargetSpeed speed) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionMoveThroughTarget);
    m_nodes[i].data = static_cast<uint32_t>(speed) & 0x3u;
    return i;
}

uint16_t BehaviorTree::addActionAdjustMenace(float delta, uint8_t mode) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionAdjustMenace);
    uint32_t delta_fixed = static_cast<uint32_t>(delta * 10.f + 0.5f) & 0xFFu;
    m_nodes[i].data = (delta_fixed << 2) | (mode & 0x3u);
    return i;
}

// ── Batch 11: BEHAVIOR XML patterns (P2+P8+P10) ──────────────────────────────

uint16_t BehaviorTree::addConditionAngleToTarget(float angle_degrees) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionAngleToTarget);
    m_nodes[i].data = static_cast<uint32_t>(angle_degrees + 0.5f);
    return i;
}

uint16_t BehaviorTree::addConditionShouldSuspend() {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionShouldSuspend);
    return i;
}

uint16_t BehaviorTree::addActionSuspendSelf() {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionSuspendSelf);
    return i;
}

uint16_t BehaviorTree::addConditionTargetDistLOS(float max_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetDistLOS);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}

// ── Batch 12 factories ────────────────────────────────────────────────────────

uint16_t BehaviorTree::addConditionTargetRoutingDistance(float max_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetRoutingDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}

uint16_t BehaviorTree::addConditionAlienIsAllowed(AlienActionType action) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionAlienIsAllowed);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(action));
    return i;
}

uint16_t BehaviorTree::addDecoratorLockVent(uint8_t vent_id) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::DecoratorLockVent);
    m_nodes[i].data = static_cast<uint32_t>(vent_id & 0x7u);
    return i;
}

// ── Batch 15 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionIsEnemyOfTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsEnemyOfTarget); return i;
}
uint16_t BehaviorTree::addActionForceIdle() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionForceIdle); return i;
}
uint16_t BehaviorTree::addConditionHasValidRouteToTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasValidRouteToTarget); return i;
}

// ── Batch 17 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionIsCharacterClass(CharacterClass cls) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionIsCharacterClass);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(cls));
    return i;
}
uint16_t BehaviorTree::addConditionIsInCover() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsInCover); return i;
}
uint16_t BehaviorTree::addConditionShouldUseCover() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionShouldUseCover); return i;
}
uint16_t BehaviorTree::addActionMoveToCover() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionMoveToCover); return i;
}
uint16_t BehaviorTree::addActionIdleInCover() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionIdleInCover); return i;
}

// ── Batch 18 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addBehaviourMoodSetCheck(BehaviourMoodSet mood_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::BehaviourMoodSetCheck);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(mood_set));
    return i;
}
uint16_t BehaviorTree::addSetBehaviourMoodSet(BehaviourMoodSet mood_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SetBehaviourMoodSet);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(mood_set));
    return i;
}
uint16_t BehaviorTree::addViewconeTypeCheck(ViewconeType vtype) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ViewconeTypeCheck);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(vtype));
    return i;
}
uint16_t BehaviorTree::addSetViewconeType(ViewconeType vtype) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SetViewconeType);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(vtype));
    return i;
}
uint16_t BehaviorTree::addSensoryTypeCheck(SensoryType stype) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SensoryTypeCheck);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(stype));
    return i;
}
uint16_t BehaviorTree::addSetSensoryType(SensoryType stype) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SetSensoryType);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(stype));
    return i;
}

// ── Batch 19 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionCurrentWeaponIsEquipped() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionCurrentWeaponIsEquipped); return i;
}
uint16_t BehaviorTree::addConditionCurrentWeaponNeedsReloading() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionCurrentWeaponNeedsReloading); return i;
}
uint16_t BehaviorTree::addConditionHasMeleeAttackAvailable() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasMeleeAttackAvailable); return i;
}
uint16_t BehaviorTree::addActionWeaponEquip() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionWeaponEquip); return i;
}
uint16_t BehaviorTree::addActionRangedShoot() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionRangedShoot); return i;
}
uint16_t BehaviorTree::addConditionHasObjective() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasObjective); return i;
}
uint16_t BehaviorTree::addConditionBehaviourMoodSetAbove(BehaviourMoodSet threshold) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionBehaviourMoodSetAbove);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(threshold));
    return i;
}

// ── Batch 20 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionSuccess() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionSuccess); return i;
}
uint16_t BehaviorTree::addActionFail() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionFail); return i;
}
uint16_t BehaviorTree::addDecoratorTimer(uint8_t timer_id, uint32_t duration_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::DecoratorTimer);
    m_nodes[i].data = (static_cast<uint32_t>(timer_id) << 24) | (duration_ms & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addActionIdle() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionIdle); return i;
}
uint16_t BehaviorTree::addConditionTargetIsWithinDistance(float max_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetIsWithinDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.0f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionHasAWeapon() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasAWeapon); return i;
}
uint16_t BehaviorTree::addConditionShouldProcessSuspiciousItem() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionShouldProcessSuspiciousItem); return i;
}

// ── Batch 21 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionMoveToTarget(LocomotionTargetSpeed speed) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionMoveToTarget);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(speed));
    return i;
}
uint16_t BehaviorTree::addActionMakeAggressive() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionMakeAggressive); return i;
}
uint16_t BehaviorTree::addConditionAllowedToAttackTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionAllowedToAttackTarget); return i;
}
uint16_t BehaviorTree::addActionDead() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionDead); return i;
}
uint16_t BehaviorTree::addActionMeleeAttack(uint8_t attack_type) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionMeleeAttack);
    m_nodes[i].data = static_cast<uint32_t>(attack_type & 0x03u);
    return i;
}
uint16_t BehaviorTree::addConditionIsCurrentCoverValid() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsCurrentCoverValid); return i;
}

// ── Batch 22 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionLastTimeSensed(uint8_t sense_idx, uint32_t max_elapsed_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionLastTimeSensed);
    m_nodes[i].data = (static_cast<uint32_t>(sense_idx) << 24) | (max_elapsed_ms & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addActionPerformRole(NpcRole role) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionPerformRole);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(role));
    return i;
}
uint16_t BehaviorTree::addConditionWasSenseThresholdLastIncreaseActivationAbove(
    uint8_t sense_idx, SenseThresholdQualifier q) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionWasSenseThresholdLastIncreaseActivationAbove);
    m_nodes[i].data = (static_cast<uint32_t>(sense_idx) << 8) | static_cast<uint32_t>(static_cast<uint8_t>(q));
    return i;
}
uint16_t BehaviorTree::addConditionTargetsWeaponHasAmmo() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionTargetsWeaponHasAmmo); return i;
}
uint16_t BehaviorTree::addConditionTargetsWeaponHasProperty(uint8_t weapon_type) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetsWeaponHasProperty);
    m_nodes[i].data = static_cast<uint32_t>(weapon_type);
    return i;
}
uint16_t BehaviorTree::addConditionHasSearchedMostRecentSensedPosition() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasSearchedMostRecentSensedPosition); return i;
}

// ── Batch 23 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionIdleTimeFacingTarget(uint32_t duration_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionIdleTimeFacingTarget);
    m_nodes[i].data = duration_ms;
    return i;
}
uint16_t BehaviorTree::addActionIdleTimeFacingTargetMostRecentSensedPosition(uint32_t duration_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionIdleTimeFacingTargetMostRecentSensedPosition);
    m_nodes[i].data = duration_ms;
    return i;
}
uint16_t BehaviorTree::addActionIdleTimeFacingSuspiciousItem(uint32_t duration_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionIdleTimeFacingSuspiciousItem);
    m_nodes[i].data = duration_ms;
    return i;
}
uint16_t BehaviorTree::addActionRangedAim() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionRangedAim); return i;
}
uint16_t BehaviorTree::addActionSuspiciousItemReaction(SuspiciousItemReaction reaction) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionSuspiciousItemReaction);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(reaction));
    return i;
}
uint16_t BehaviorTree::addActionSuspectTargetResponse() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionSuspectTargetResponse); return i;
}

// ── Batch 24 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionRequestCover() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionRequestCover); return i;
}
uint16_t BehaviorTree::addConditionHasValidCoverToChangeTo() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasValidCoverToChangeTo); return i;
}

// ── Batch 25 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionHasScript() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasScript); return i;
}
uint16_t BehaviorTree::addActionScript(uint32_t script_name_hash) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionScript);
    m_nodes[i].data = script_name_hash;
    return i;
}

// ── Batch 26 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionLastTimeSearchedWithinTime(uint32_t max_elapsed_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionLastTimeSearchedWithinTime);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addConditionAllowedToSearch() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionAllowedToSearch); return i;
}
uint16_t BehaviorTree::addConditionHasDoneSuspectResponseMoveTo() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasDoneSuspectResponseMoveTo); return i;
}
uint16_t BehaviorTree::addConditionHasDoneSuspectResponseWithinTime(uint32_t max_elapsed_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionHasDoneSuspectResponseWithinTime);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addConditionHasKilltrap() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasKilltrap); return i;
}
uint16_t BehaviorTree::addConditionHasMeleeAttackAvailableOrIsAttacking() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasMeleeAttackAvailableOrIsAttacking); return i;
}
uint16_t BehaviorTree::addConditionIsBranchActive(uint32_t branch_name_hash) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionIsBranchActive);
    m_nodes[i].data = branch_name_hash;
    return i;
}
uint16_t BehaviorTree::addConditionIsRequestingCover() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsRequestingCover); return i;
}
uint16_t BehaviorTree::addConditionAllowedToPursueTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionAllowedToPursueTarget); return i;
}

// ── Batch 27 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionBreakout() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionBreakout); return i;
}
uint16_t BehaviorTree::addActionMoveToMostRecentSensedPosition() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionMoveToMostRecentSensedPosition); return i;
}
uint16_t BehaviorTree::addActionMoveToNearestStandingPointToTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionMoveToNearestStandingPointToTarget); return i;
}
uint16_t BehaviorTree::addActionMoveToAttackTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionMoveToAttackTarget); return i;
}
uint16_t BehaviorTree::addActionChangeCover() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionChangeCover); return i;
}

// ── Batch 28 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionIsInTargetsWeaponRange(float max_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionIsInTargetsWeaponRange);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionIsCoverExposed() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsCoverExposed); return i;
}
uint16_t BehaviorTree::addConditionHasLostTarget(uint32_t max_elapsed_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionHasLostTarget);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addActionUpdateLastKnownPosition() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionUpdateLastKnownPosition); return i;
}
uint16_t BehaviorTree::addActionRequestInvestigate() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionRequestInvestigate); return i;
}
uint16_t BehaviorTree::addActionMarkTargetLost() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionMarkTargetLost); return i;
}
uint16_t BehaviorTree::addActionForceRetreat() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionForceRetreat); return i;
}
uint16_t BehaviorTree::addActionHoldPosition() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionHoldPosition); return i;
}

// ── Batch 29 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionNpcDevelopmentStageAbove(NpcDevelopmentStage stage) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionNpcDevelopmentStageAbove);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint16_t>(stage));
    return i;
}
uint16_t BehaviorTree::addConditionNpcHasAbility(uint16_t ability_mask) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionNpcHasAbility);
    m_nodes[i].data = static_cast<uint32_t>(ability_mask);
    return i;
}
uint16_t BehaviorTree::addConditionIsHostileToPlayer() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsHostileToPlayer); return i;
}
uint16_t BehaviorTree::addActionPerformAmbush() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionPerformAmbush); return i;
}
uint16_t BehaviorTree::addActionStartSearch() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionStartSearch); return i;
}
uint16_t BehaviorTree::addActionCallForHelp() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionCallForHelp); return i;
}
uint16_t BehaviorTree::addActionTauntTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionTauntTarget); return i;
}
uint16_t BehaviorTree::addActionSurrenderSelf() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionSurrenderSelf); return i;
}

// ── Batch 30 ─────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionEventCountAbove(uint8_t min_count) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionEventCountAbove);
    m_nodes[i].data = static_cast<uint32_t>(min_count);
    return i;
}
uint16_t BehaviorTree::addConditionSpatialMemoryCountAbove(uint8_t min_count) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionSpatialMemoryCountAbove);
    m_nodes[i].data = static_cast<uint32_t>(min_count);
    return i;
}
uint16_t BehaviorTree::addConditionMotivationTicksAbove(uint8_t min_ticks) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionMotivationTicksAbove);
    m_nodes[i].data = static_cast<uint32_t>(min_ticks);
    return i;
}
uint16_t BehaviorTree::addConditionHasVisualHistory() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasVisualHistory); return i;
}
uint16_t BehaviorTree::addActionPursueTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionPursueTarget); return i;
}
uint16_t BehaviorTree::addActionCircleTarget() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionCircleTarget); return i;
}
uint16_t BehaviorTree::addActionBackOff() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionBackOff); return i;
}
uint16_t BehaviorTree::addActionCrouchMove() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionCrouchMove); return i;
}
uint16_t BehaviorTree::addActionVault() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionVault); return i;
}

// ── Batch 31 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionHasVentCloseToAlien(float radius_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionHasVentCloseToAlien);
    m_nodes[i].data = static_cast<uint32_t>(radius_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionHasFlankedVentCloseToPlayer(float radius_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionHasFlankedVentCloseToPlayer);
    m_nodes[i].data = static_cast<uint32_t>(radius_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionTargetIsOnlyAccessibleCrouching() {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetIsOnlyAccessibleCrouching);
    return i;
}
uint16_t BehaviorTree::addConditionAngleNPCToTargetsAimLessThan(float angle_deg) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionAngleNPCToTargetsAimLessThan);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(angle_deg + 0.5f));
    return i;
}

// ── Batch 35 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionApplyPrimaryDamageControlResponse() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionApplyPrimaryDamageControlResponse); return i;
}
uint16_t BehaviorTree::addActionBrokenCover() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionBrokenCover); return i;
}
uint16_t BehaviorTree::addActionMoveToObjective() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionMoveToObjective); return i;
}
uint16_t BehaviorTree::addActionSetLogicCharacterFlags(uint8_t bit_idx, bool do_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionSetLogicCharacterFlags);
    m_nodes[i].data = (static_cast<uint32_t>(bit_idx) << 8) | (do_set ? 1u : 0u);
    return i;
}
uint16_t BehaviorTree::addConditionAllowedToDoSuspiciousWarning() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionAllowedToDoSuspiciousWarning); return i;
}
uint16_t BehaviorTree::addConditionCanTakeStep() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionCanTakeStep); return i;
}
uint16_t BehaviorTree::addConditionGameIsDifficulty() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionGameIsDifficulty); return i;
}
uint16_t BehaviorTree::addConditionHasAnySenseBeenAboveWithinTime(SenseThresholdQualifier q,
                                                                   uint32_t time_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionHasAnySenseBeenAboveWithinTime);
    m_nodes[i].data = (static_cast<uint32_t>(q) << 24) | (time_ms & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addConditionIsCorpseTrap() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsCorpseTrap); return i;
}
uint16_t BehaviorTree::addConditionIsGaugeAmountBelow(GaugeType gauge, float threshold) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionIsGaugeAmountBelow);
    uint32_t t = static_cast<uint32_t>(threshold * 1000.f + 0.5f);
    m_nodes[i].data = (static_cast<uint32_t>(gauge) << 24) | (t & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addConditionLastTimeWasAbleToShootTarget(uint32_t max_elapsed_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionLastTimeWasAbleToShootTarget);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addConditionRequiresPrimaryDamageControlResponse(uint8_t threshold_pct) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionRequiresPrimaryDamageControlResponse);
    m_nodes[i].data = threshold_pct;
    return i;
}
uint16_t BehaviorTree::addConditionTargetIsPlayer() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionTargetIsPlayer); return i;
}
uint16_t BehaviorTree::addConditionTargetIsWithinDistanceUnobscured(float max_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetIsWithinDistanceUnobscured);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionTargetLogicCharacterFlags(uint8_t bit_idx, bool check_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetLogicCharacterFlags);
    m_nodes[i].data = (static_cast<uint32_t>(bit_idx) << 8) | (check_set ? 1u : 0u);
    return i;
}

// ── Batch 34 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionForceSearch() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionForceSearch); return i;
}
uint16_t BehaviorTree::addActionResetSearchJobs() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionResetSearchJobs); return i;
}
uint16_t BehaviorTree::addActionSetFrameFlag(uint8_t bit_idx) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionSetFrameFlag);
    m_nodes[i].data = bit_idx;
    return i;
}
uint16_t BehaviorTree::addActionSetGaugeAmount(GaugeType gauge, float value) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionSetGaugeAmount);
    uint32_t v = static_cast<uint32_t>(value * 1000.f + 0.5f);
    if (v > 0xFFFFu) v = 0xFFFFu;
    m_nodes[i].data = (static_cast<uint32_t>(gauge) << 16) | v;
    return i;
}
uint16_t BehaviorTree::addConditionHasSenseActivationBeenAbove(uint8_t sense_idx,
                                                                SenseThresholdQualifier q) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionHasSenseActivationBeenAbove);
    m_nodes[i].data = (static_cast<uint32_t>(sense_idx) << 8) | static_cast<uint32_t>(q);
    return i;
}
uint16_t BehaviorTree::addConditionIsCoverTooClose(float min_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionIsCoverTooClose);
    m_nodes[i].data = static_cast<uint32_t>(min_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionIsInCombatArea() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionIsInCombatArea); return i;
}
uint16_t BehaviorTree::addConditionMostRecentSenseActivationHasBeenAbove(SenseThresholdQualifier q) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionMostRecentSenseActivationHasBeenAbove);
    m_nodes[i].data = static_cast<uint32_t>(q);
    return i;
}
uint16_t BehaviorTree::addConditionNeedsToGetOutOfTheWay() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionNeedsToGetOutOfTheWay); return i;
}
uint16_t BehaviorTree::addConditionObjectiveIsInCombatArea() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionObjectiveIsInCombatArea); return i;
}
uint16_t BehaviorTree::addConditionObjectiveIsWithinDistance(float max_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionObjectiveIsWithinDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionShouldFollow() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionShouldFollow); return i;
}
uint16_t BehaviorTree::addConditionTargetIsInCombatArea() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionTargetIsInCombatArea); return i;
}
uint16_t BehaviorTree::addConditionTargetIsWithinAggroRadius(float radius_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetIsWithinAggroRadius);
    m_nodes[i].data = static_cast<uint32_t>(radius_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addConditionTargetNearestStandPointIsWithinDistance(float max_dist_m) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionTargetNearestStandPointIsWithinDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}
uint16_t BehaviorTree::addDecoratorSetSenseSet(uint8_t sense_set_id) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::DecoratorSetSenseSet);
    m_nodes[i].data = sense_set_id;
    return i;
}
uint16_t BehaviorTree::addDecoratorSuspiciousItemInProgress() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::DecoratorSuspiciousItemInProgress); return i;
}
uint16_t BehaviorTree::addSelectorLinear() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::SelectorLinear); return i;
}
uint16_t BehaviorTree::addSequenceLinear() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::SequenceLinear); return i;
}

// ── Batch 33 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addActionAbortMeleeAttack() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionAbortMeleeAttack); return i;
}
uint16_t BehaviorTree::addActionGetOutOfTheWay() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionGetOutOfTheWay); return i;
}
uint16_t BehaviorTree::addActionHitTargetAndRun() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionHitTargetAndRun); return i;
}
uint16_t BehaviorTree::addActionMoveInDirection(uint8_t direction) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionMoveInDirection);
    m_nodes[i].data = direction & 0x3u;
    return i;
}
uint16_t BehaviorTree::addActionSuspend() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionSuspend); return i;
}
uint16_t BehaviorTree::addActionTakeStep() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionTakeStep); return i;
}
uint16_t BehaviorTree::addActionThreatAware() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionThreatAware); return i;
}
uint16_t BehaviorTree::addActionThreatEscalation() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ActionThreatEscalation); return i;
}
uint16_t BehaviorTree::addConditionCanShootNow() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionCanShootNow); return i;
}
uint16_t BehaviorTree::addConditionCheckHealthState(uint8_t threshold_pct) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionCheckHealthState);
    m_nodes[i].data = threshold_pct;
    return i;
}
uint16_t BehaviorTree::addConditionHasGroupAwarenessState(AwarenessState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionHasGroupAwarenessState);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}
uint16_t BehaviorTree::addConditionHasMeleeBlockAvailable() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasMeleeBlockAvailable); return i;
}
uint16_t BehaviorTree::addConditionHasMeleeCounterAttackAvailable() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionHasMeleeCounterAttackAvailable); return i;
}
uint16_t BehaviorTree::addConditionLastTimeTargetShotAtMe(uint32_t max_elapsed_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionLastTimeTargetShotAtMe);
    m_nodes[i].data = max_elapsed_ms;
    return i;
}
uint16_t BehaviorTree::addConditionTargetIsInWeaponRange() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionTargetIsInWeaponRange); return i;
}
uint16_t BehaviorTree::addConditionTargetIsTargetingMe() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionTargetIsTargetingMe); return i;
}
uint16_t BehaviorTree::addConditionTargetIsUsingMeleeAttack() {
    uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::ConditionTargetIsUsingMeleeAttack); return i;
}
uint16_t BehaviorTree::addDecoratorLoop(uint32_t count) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::DecoratorLoop);
    m_nodes[i].data = count;
    return i;
}

// ── Batch 32 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionHasToken(uint32_t token_id) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionHasToken);
    m_nodes[i].data = token_id;
    return i;
}
uint16_t BehaviorTree::addActionReleaseToken(uint32_t token_id) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionReleaseToken);
    m_nodes[i].data = token_id;
    return i;
}

