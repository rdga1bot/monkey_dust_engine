#include <monkey_dust/ai/behavior_tree.h>

// ── Batch 2 factories ────────────────────────────────────────────────────────

uint16_t BehaviorTree::addConditionIsDead() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsDead); return i;
}
uint16_t BehaviorTree::addConditionIsInVent(uint8_t char_type) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionIsInVent);
    m_nodes[i].data = char_type & 0x3u;
    return i;
}
uint16_t BehaviorTree::addConditionCanBreakout() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionCanBreakout); return i;
}
uint16_t BehaviorTree::addConditionIsBackstage() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsBackstage); return i;
}
uint16_t BehaviorTree::addConditionIsPartOfNPCGroup() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsPartOfNPCGroup); return i;
}
uint16_t BehaviorTree::addConditionAnotherAlienIsAttacking() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionAnotherAlienIsAttacking); return i;
}
uint16_t BehaviorTree::addConditionHasSearchedPos() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasSearchedPos); return i;
}
uint16_t BehaviorTree::addConditionHasDoneSuspectMoveTo() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionHasDoneSuspectMoveTo); return i;
}
uint16_t BehaviorTree::addActionSwitchToNextTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionSwitchToNextTarget); return i;
}
uint16_t BehaviorTree::addActionDoneSystematicSearch() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionDoneSystematicSearch); return i;
}

// ── Batch 4: EventOrder ───────────────────────────────────────────────────────

uint16_t BehaviorTree::addConditionEventAOccuredAfterB(EventType a, EventType b) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionEventAOccuredAfterB);
    m_nodes[i].data = (static_cast<uint32_t>(a) << 8) | static_cast<uint32_t>(b);
    return i;
}

// ── Batch 5: Squad extensions ─────────────────────────────────────────────────

uint16_t BehaviorTree::addConditionSquadDoingEscalation() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionSquadDoingEscalation); return i;
}
uint16_t BehaviorTree::addConditionSquadDoingSuspiciousWarning() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionSquadDoingSuspiciousWarning); return i;
}
uint16_t BehaviorTree::addDecoratorSquadSearch() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::DecoratorSquadSearch); return i;
}

// ── Batch 6: SuspiciousItem Group system ──────────────────────────────────────

uint16_t BehaviorTree::addConditionSuspiciousItemShouldDoStage(SuspiciousItemStage stage) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemShouldDoStage);
    m_nodes[i].data = static_cast<uint32_t>(stage);
    return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemIsWithinDistance(float max_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemIsWithinDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 100.f + 0.5f); // cm
    return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemFirstGroupMember() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemFirstGroupMember); return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemGroupAllowedToProgress() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemGroupAllowedToProgress); return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemGroupMembersRoutingTo() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemGroupMembersRoutingTo); return i;
}
uint16_t BehaviorTree::addConditionSuspiciousItemWaitForGroupRouting() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemWaitForGroupRouting); return i;
}

// ── Batch 7: SuspiciousItemReaction / AmbushType / NoiseType ─────────────────
uint16_t BehaviorTree::addSIReactionCheck(SuspiciousItemReaction reaction) {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::SIReactionCheck);
    m_nodes[i].data = static_cast<uint32_t>(reaction); return i;
}
uint16_t BehaviorTree::addSetSIReaction(SuspiciousItemReaction reaction) {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::SetSIReaction);
    m_nodes[i].data = static_cast<uint32_t>(reaction); return i;
}
uint16_t BehaviorTree::addAmbushTypeCheck(AmbushType type) {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::AmbushTypeCheck);
    m_nodes[i].data = static_cast<uint32_t>(type); return i;
}
uint16_t BehaviorTree::addSetAmbushType(AmbushType type) {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::SetAmbushType);
    m_nodes[i].data = static_cast<uint32_t>(type); return i;
}
uint16_t BehaviorTree::addNoiseTypeCheck(NoiseType type) {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::NoiseTypeCheck);
    m_nodes[i].data = static_cast<uint32_t>(type); return i;
}
uint16_t BehaviorTree::addSetNoiseType(NoiseType type) {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::SetNoiseType);
    m_nodes[i].data = static_cast<uint32_t>(type); return i;
}

// ── Batch 8: SI lifecycle ─────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionSuspiciousItemValid() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionSuspiciousItemValid); return i;
}
uint16_t BehaviorTree::addActionConsumeSuspiciousItem() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionConsumeSuspiciousItem); return i;
}
uint16_t BehaviorTree::addActionForceMoveToSI() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionForceMoveToSI); return i;
}

// ── Batch 9 (Step 5): Inter-NPC relationship nodes ────────────────────────────
uint16_t BehaviorTree::addRelationshipTrustCheck(uint8_t threshold, uint8_t mode) {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::RelationshipTrustCheck);
    m_nodes[i].data = (static_cast<uint32_t>(threshold) << 8) | (mode & 0x1u); return i;
}
uint16_t BehaviorTree::addRelationshipFearCheck(uint8_t threshold, uint8_t mode) {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::RelationshipFearCheck);
    m_nodes[i].data = (static_cast<uint32_t>(threshold) << 8) | (mode & 0x1u); return i;
}

// ── Batch 10: MD_z.md BEHAVIOR XML patterns ──────────────────────────────────

uint16_t BehaviorTree::addConditionIsAnySenseActivationAbove(SenseThresholdQualifier q) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionIsAnySenseActivationAbove);
    m_nodes[i].data = static_cast<uint32_t>(q);
    return i;
}

uint16_t BehaviorTree::addActionMoveThroughTarget(LocomotionTargetSpeed speed) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionMoveThroughTarget);
    m_nodes[i].data = static_cast<uint32_t>(speed) & 0x3u;
    return i;
}

uint16_t BehaviorTree::addActionAdjustMenace(float delta, uint8_t mode) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionAdjustMenace);
    uint32_t delta_fixed = static_cast<uint32_t>(delta * 10.f + 0.5f) & 0xFFu;
    m_nodes[i].data = (delta_fixed << 2) | (mode & 0x3u);
    return i;
}

// ── Batch 11: BEHAVIOR XML patterns (P2+P8+P10) ──────────────────────────────

uint16_t BehaviorTree::addConditionAngleToTarget(float angle_degrees) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionAngleToTarget);
    m_nodes[i].data = static_cast<uint32_t>(angle_degrees + 0.5f);
    return i;
}

uint16_t BehaviorTree::addConditionShouldSuspend() {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionShouldSuspend);
    return i;
}

uint16_t BehaviorTree::addActionSuspendSelf() {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ActionSuspendSelf);
    return i;
}

uint16_t BehaviorTree::addConditionTargetDistLOS(float max_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetDistLOS);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}

// ── Batch 12 factories ────────────────────────────────────────────────────────

uint16_t BehaviorTree::addConditionTargetRoutingDistance(float max_dist_m) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionTargetRoutingDistance);
    m_nodes[i].data = static_cast<uint32_t>(max_dist_m * 10.f + 0.5f);
    return i;
}

uint16_t BehaviorTree::addConditionAlienIsAllowed(AlienActionType action) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::ConditionAlienIsAllowed);
    m_nodes[i].data = static_cast<uint32_t>(static_cast<uint8_t>(action));
    return i;
}

uint16_t BehaviorTree::addDecoratorLockVent(uint8_t vent_id) {
    uint16_t i = allocNodeIdx();
    initNode(m_nodes[i], BTNodeType::DecoratorLockVent);
    m_nodes[i].data = static_cast<uint32_t>(vent_id & 0x7u);
    return i;
}

// ── Batch 15 ──────────────────────────────────────────────────────────────────
uint16_t BehaviorTree::addConditionIsEnemyOfTarget() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ConditionIsEnemyOfTarget); return i;
}
uint16_t BehaviorTree::addActionForceIdle() {
    uint16_t i = allocNodeIdx(); initNode(m_nodes[i], BTNodeType::ActionForceIdle); return i;
}
