#pragma once
#include <monkey_dust/ecs/engine_context.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/ai/role_registry.h>
#include <monkey_dust/ai/squad_signal.h>
#include <monkey_dust/ai/named_branch.h>
#include <monkey_dust/ai/npc_sound.h>
#include <monkey_dust/components/npc_memory.h>
#include <monkey_dust/components/npc_relationship.h>
#include <monkey_dust/ai/suspicious_item_group.h>
#include <monkey_dust/ai/alien_config.h>
#include <monkey_dust/ai/vent_lock.h>
#include <monkey_dust/ai/npc_development.h>
#include <entt/entt.hpp>
#include <cstdint>

// ── SenseThresholdQualifier ───────────────────────────────────────────────────
// Maps MD ThresholdQualifier enum used by sense-activation BT conditions.
enum class SenseThresholdQualifier : uint8_t {
    Trace     = 0,  // any activation (activation > 0)
    Lower     = 1,  // activation >= threshold_lo
    Activated = 2,  // activation >= threshold_hi (was triggered)
    Upper     = 3,  // activation >= 1.0 (fully saturated)
};

// ── Pattern 2: ShutdownSpeed ──────────────────────────────────────────────────
enum class ShutdownSpeed : uint8_t {
    Graceful = 0,  // SST_GRACEFULL — finish current action before stopping
    Normal   = 1,  // SST_NORMAL
    Critical = 2,  // SST_CRITICAL — abort immediately
};

// ── Pattern 2: BranchType ─────────────────────────────────────────────────────
// Stored in BTNode::data (lower byte) for Branch nodes — purely semantic/debug.
// The BT VM does not change behaviour based on BranchType; the type names the gate
// so profiling/debugging can identify which interrupt is active.
enum class BranchType : uint8_t {
    Standard             =  0,
    Cinematic            =  1,
    Attack               =  2,
    Despawn              =  4,
    AreaSweep            =  8,
    BackstageAreaSweep   =  9,
    Shot                 = 10,
    SuspectTargetResponse= 11,
    ThreatAware          = 12,
    AttackCore           = 50,
    Breakout             = 46,
    StunDamage           = 45,
    Suspend              = 47,  // load/save suspend gate
    Dead                 = 30,
    Script               = 31,
    Idle                 = 32,
    Killtrap             = 63,
    Panic                = 68,
};

enum class BTStatus : uint8_t {
    Success,
    Failure,
    Running
};

enum class BTNodeType : uint8_t {
    Selector,
    Sequence,
    Condition,
    Action,
    Inverter,
    Repeat,
    Wait,
    // M21 — Extended AI node types
    Branch,         // persistent interrupt: re-checks nd.condition every tick before child
    TimerStart,     // leaf: as->timers[slot] = nowMs + duration_ms
    TimerCheck,     // leaf condition: expired? clears timer on fire
    FlagCheck,      // leaf condition: as->lcflags.test(bit_idx); nd.flags=1 → check clear
    FlagSet,        // leaf action: set(nd.flags=0) or clear(nd.flags=1) bit at nd.data
    SenseCheck,     // leaf condition: activation[idx] >= threshold
    // AI adaptations (Patterns 1, 3, 6)
    MotivationCheck,// leaf condition: as->motivation == (MotivationType)data
    SetMotivation,  // leaf action: as->motivation = (MotivationType)data → Success
    Reference,      // delegates tick to another BehaviorTree* stored in _padding
    GaugeCheck,     // leaf condition: as->gauges.get(type) >= threshold
    GaugeSet,       // leaf action: as->gauges.set(type, value) → Success
    // ── Extended AI node types ──────────────────────────────────────────────
    // C11: SequenceStateless — always restarts children from index 0 on re-entry
    SequenceStateless,
    // C13: FrameFlag — single-tick signal; frame_flags cleared each logic tick
    FrameFlagCheck, // data=bit_idx(0-63), flags=0→check set / 1→check clear
    FrameFlagSet,   // data=bit_idx(0-63), flags=0→set / 1→clear
    // C14: WeightedSelector — picks child by weighted probability (LCG RNG)
    //   data: bits 0-7=w[0], 8-15=w[1], 16-23=w[2], 24-31=w[3]; sum need not be 100
    WeightedSelector,
    // C15: AwarenessCheck — gates branch on as->awareness == (AwarenessState)data
    AwarenessCheck,
    // C16: AlertnessCheck — gates branch on as->alertness == (AlertnessState)data
    AlertnessCheck,
    // C17: MoodCheck — gates branch on as->mood == (NpcMood)data
    MoodCheck,
    // C18: Role nodes — coordinate exclusive NPC roles via RoleRegistry singleton
    //   RoleCheck:   data = (NpcRole<<8)|(mode:0=performing,1=could_perform)
    //   RoleClaim:   data = (query_id<<8)|(NpcRole&0xFF); Success=claimed, Failure=busy
    //   RoleRelease: data = NpcRole; always Success
    RoleCheck,
    RoleClaim,
    RoleRelease,
    // C19: WithdrawState — 3-stage retreat FSM
    //   WithdrawCheck: data=(WithdrawState)→Success if as->withdraw_state matches
    //   SetWithdraw:   data=(WithdrawState)→writes and returns Success
    WithdrawCheck,
    SetWithdraw,
    // Echo-inspired NpcMemory nodes
    //   MemoryCheck: data=0→has_spatial_memory (spatial_count>0), data=1→has_event_memory
    //   MemoryForget: clears all NpcMemoryComponent data → always Success
    MemoryCheck,
    MemoryForget,
    // MdDump: AreaSweepCheck — gates on as->area_sweep_type == (AreaSweepType)data
    AreaSweepCheck,
    // MD_gemini patterns:
    //   DecoratorPercentage: succeeds child execution with data% probability (LCG)
    DecoratorPercentage,
    //   SelectorPercentage: picks one random child (equal probability), returns its result
    SelectorPercentage,
    //   SenseTimeCheck: Success if now_ms - sc->last_activated_ms[sense_idx] <= max_elapsed_ms
    //     data = (sense_idx << 24) | max_elapsed_ms (24 bits → max ~16.7s)
    SenseTimeCheck,
    //   ActionSetDead:    sets lcf::IS_DEAD → entity lifecycle end
    ActionSetDead,
    //   ActionDespawn:    sets lcf::SHOULD_DESPAWN → deferred entity removal
    ActionDespawn,
    // MD_deepseek patterns:
    //   AggroLevelCheck: data=(NpcAggroLevel)→Success if as->aggro_level matches
    //   SetAggroLevel:   data=(NpcAggroLevel)→writes and returns Success
    AggroLevelCheck,
    SetAggroLevel,
    //   NpcCombatStateCheck: data=(NpcCombatState)→Success if as->combat_state matches
    //   SetNpcCombatState:   data=(NpcCombatState)→writes and returns Success
    NpcCombatStateCheck,
    SetNpcCombatState,
    // MD_z patterns:
    //   DecoratorMood:     decorator — run child only when as->mood == (NpcMood)data
    //   DecoratorAwareness:decorator — run child only when as->awareness == (AwarenessState)data
    DecoratorMood,
    DecoratorAwareness,
    //   DecoratorTimerAuto: decorator — auto-start timer on every entry, propagate child result
    //     data=(timer_id<<24)|duration_ms; flags bit0=only_increase
    DecoratorTimerAuto,
    //   ActionTimerRandom: start timer with LCG-random duration in [min, max]
    //     data=(timer_id<<24)|(max_u12<<12)|min_u12; unit=100ms; LCG seeded by entity^nowMs
    ActionTimerRandom,
    //   ActionSquadNotify: broadcast SquadSignal to entity's squad channel
    //     data=(SquadSignal)value; reads SquadMemberComponent::squad_id
    ActionSquadNotify,
    //   ConditionSquadSignal: check squad channel for expected signal
    //     data=(SquadSignal)value; reads SquadMemberComponent::squad_id
    ConditionSquadSignal,
    //   ConditionAnySenseWithinTime: Success if any (or specific) sense fired within time_ms
    //     data=(sense_idx<<28)|time_window_ms; flags=0→any sense, 1→specific sense_idx
    ConditionAnySenseWithinTime,
    //   ActionExpireTimer: immediately mark a timer slot as expired (sets timers[id]=1)
    //     data=timer_id & 0x1F
    ActionExpireTimer,
    //   TargetFlagCheck: check lcflags on target entity stored in blackboard "target_entity" key
    //     data=bit_idx & 0x3F; flags=0→check set, 1→check clear
    TargetFlagCheck,
    //   SetLocomotionState: write as->locomotion_state = (LocomotionState)data
    SetLocomotionState,
    // MD_arch Pattern 4 — DecoratorNamedBranch:
    //   data  = fnv1a(branch_name); checks NamedBranchRegistry::IsActive(data)
    //   flags = 0: run child only when active (gate); 1: run child only when inactive (inverted)
    //   returns child result; Failure if condition not met
    DecoratorNamedBranch,
    // MD_grok patterns — from BEHAVIOR XML analysis
    //   ActionIdleTime: wait data milliseconds then Success (uses st.timer like Wait)
    ActionIdleTime,
    //   ConditionHaveTarget: Success if bb["target_entity"] holds a valid entity
    ConditionHaveTarget,
    //   ConditionHaveNextTarget: Success if bb["next_target_entity"] holds a valid entity
    ConditionHaveNextTarget,
    //   ActionTriggerSound: fire NpcSoundEvent via NpcSoundBus; always Success
    //     data = NpcSoundEvent (uint8)
    ActionTriggerSound,
    //   ConditionIsSenseActivationAbove: check sc->activation[sense_idx] vs threshold qualifier
    //     data = (sense_idx << 8) | SenseThresholdQualifier
    ConditionIsSenseActivationAbove,
    //   ConditionHasAnySenseBeenAbove: check if any sense was ever triggered
    //     data = SenseThresholdQualifier; checks last_activated_ms[i] != 0 (ACTIVATED/UPPER)
    //     or activation[i] > 0 (TRACE) or activation[i] >= threshold_lo (LOWER)
    ConditionHasAnySenseBeenAbove,
    //   ActionSuspiciousItemDoneStage: set investigation_stage on most recent NpcEventRecord
    //     data = SuspiciousItemStage; requires NpcMemoryComponent with event_count > 0
    ActionSuspiciousItemDoneStage,
    //   ConditionSuspiciousItemBTPriority: Success if any event has intensity >= data
    //     data = min_intensity (0=low, 1=medium, 2=high)
    ConditionSuspiciousItemBTPriority,
    //   ConditionLastTimeSquadNotified: Success if squad channel has signal within time threshold
    //     data = (uint8_t(signal) << 8) | time_threshold_s; checks channel.timestamp
    ConditionLastTimeSquadNotified,
    //   DecoratorAggressionEscalation: gate child on as->aggro_level != None
    //     data = 0; propagates child result; Failure if aggro_level == None or no AgentState
    DecoratorAggressionEscalation,

    // ── Batch 2: MD patterns — BEHAVIOR XML conditions/actions ─────────────────
    //   ConditionIsDead:                  Success if lcf::IS_DEAD set; data=0
    ConditionIsDead,
    //   ConditionIsInVent:                Success if lcf::IS_IN_VENT set; data=0
    ConditionIsInVent,
    //   ConditionCanBreakout:             Success if SHOULD_BREAKOUT && !DONE_BREAKOUT; data=0
    ConditionCanBreakout,
    //   ConditionIsBackstage:             Success if lcf::CLOSE_TO_BACKSTAGE set; data=0
    ConditionIsBackstage,
    //   ConditionIsPartOfNPCGroup:        Success if SquadMemberComponent present; data=0
    ConditionIsPartOfNPCGroup,
    //   ConditionAnotherAlienIsAttacking: Success if RoleRegistry Stalk/Attack owner != null && != self; data=0
    ConditionAnotherAlienIsAttacking,
    //   ConditionHasSearchedPos:          Success if bb["has_searched_pos"] == true; data=0
    ConditionHasSearchedPos,
    //   ConditionHasDoneSuspectMoveTo:    Success if bb["done_suspect_moveto"] == true; data=0
    ConditionHasDoneSuspectMoveTo,
    //   ActionSwitchToNextTarget:         Copies bb[next_target_entity] → bb[target_entity]; data=0
    ActionSwitchToNextTarget,
    //   ActionDoneSystematicSearch:       Sets bb["done_systematic_search"]=true; data=0
    ActionDoneSystematicSearch,

    // ── Batch 4: EventOrder ───────────────────────────────────────────────────
    //   ConditionEventAOccuredAfterB:     Success if as->event_ts[A]>as->event_ts[B] and A!=0
    //                                     data=(EventType_A<<8)|EventType_B
    ConditionEventAOccuredAfterB,

    // ── Batch 5: Squad extensions ─────────────────────────────────────────────
    //   ConditionSquadDoingEscalation:    Success if squad channel signal == Escalating; data=0
    //   ConditionSquadDoingSuspiciousWarning: Success if channel == SuspiciousWarn; data=0
    //   DecoratorSquadSearch:             On entry: notify squad Warning; then run child
    ConditionSquadDoingEscalation,
    ConditionSquadDoingSuspiciousWarning,
    DecoratorSquadSearch,

    // ── Batch 6: SuspiciousItem Group system ──────────────────────────────────
    //   ConditionSuspiciousItemShouldDoStage:    nm->events[0].investigation_stage==data
    //   ConditionSuspiciousItemIsWithinDistance: dist(sc->last_known,ev->x/z)<=data*0.01m
    //   ConditionSuspiciousItemFirstGroupMember: SuspiciousItemGroupRegistry::IsFirstMember
    //   ConditionSuspiciousItemGroupAllowedToProgress: IsAllowedToProgress
    //   ConditionSuspiciousItemGroupMembersRoutingTo:  routing_count==member_count
    //   ConditionSuspiciousItemWaitForGroupRouting:    routing_count<member_count
    ConditionSuspiciousItemShouldDoStage,
    ConditionSuspiciousItemIsWithinDistance,
    ConditionSuspiciousItemFirstGroupMember,
    ConditionSuspiciousItemGroupAllowedToProgress,
    ConditionSuspiciousItemGroupMembersRoutingTo,
    ConditionSuspiciousItemWaitForGroupRouting,

    // ── Batch 7: SuspiciousItemReaction / AmbushType / NoiseType ─────────────
    //   SIReactionCheck:  Success if as->si_reaction == (SuspiciousItemReaction)data
    //   SetSIReaction:    as->si_reaction = (SuspiciousItemReaction)data → Success
    SIReactionCheck,
    SetSIReaction,
    //   AmbushTypeCheck:  Success if as->ambush_type == (AmbushType)data
    //   SetAmbushType:    as->ambush_type = (AmbushType)data → Success
    AmbushTypeCheck,
    SetAmbushType,
    //   NoiseTypeCheck:   Success if as->last_noise_type == (NoiseType)data
    //   SetNoiseType:     as->last_noise_type = (NoiseType)data → Success
    NoiseTypeCheck,
    SetNoiseType,

    // ── Batch 8: SI lifecycle nodes ───────────────────────────────────────────
    //   ConditionSuspiciousItemValid:   Success if nm->event_count>0 AND
    //                                   events[0].stage != SearchArea (still active)
    ConditionSuspiciousItemValid,
    //   ActionConsumeSuspiciousItem:    Remove events[0], shift remainder down;
    //                                   always Success (no-op when empty)
    ActionConsumeSuspiciousItem,
    //   ActionForceMoveToSI:            Set ff::COULD_DO_SUSPECT_TARGET_RESPONSE_MOVETO
    //                                   in as->frame_flags; game-side nav reads events[0].x/z
    //                                   Failure if no NpcMemoryComponent or event_count==0
    ActionForceMoveToSI,

    // ── Batch 9 (Step 5): Inter-NPC relationship nodes ────────────────────────
    //   RelationshipTrustCheck: read NpcRelationshipComponent of self, check trust to target
    //     target = bb["target_entity"]; Failure if no bb/key/NpcRelationshipComponent
    //     data=(threshold<<8)|mode; mode=0→trust>=threshold, mode=1→trust<threshold
    RelationshipTrustCheck,
    //   RelationshipFearCheck: same as TrustCheck but reads fear field
    //     data=(threshold<<8)|mode; mode=0→fear>=threshold, mode=1→fear<threshold
    RelationshipFearCheck,

    // ── Batch 10: MD_z.md BEHAVIOR XML patterns ───────────────────────────────
    //   ConditionIsAnySenseActivationAbove: Success if any sc->activation[i] >= qualifier
    //     data = SenseThresholdQualifier (Trace=0, Lower=1, Activated=2, Upper=3)
    ConditionIsAnySenseActivationAbove,
    //   ActionMoveThroughTarget: nav hint — sets ff::SHOULD_MOVE_THROUGH_TARGET in frame_flags
    //     also writes as->target_speed; data = LocomotionTargetSpeed (0-3); always Success
    ActionMoveThroughTarget,
    //   ActionAdjustMenace: calls DirectorSystem::AdjustMenace(delta, mode); always Success
    //     data = (delta_fixed << 2) | mode; delta_fixed = uint8(delta * 10); mode: 0=add,1=set,2=sub
    ActionAdjustMenace,

    // ── Batch 11: BEHAVIOR XML patterns (P2+P8+P10) ──────────────────────────
    //   ConditionAngleToTarget: Success if angle(NPC_forward, dir_to_target) <= data degrees
    //     Uses WorldTransform::rot_y for forward; SenseComponent::last_known_x/z for target pos.
    //     data = angle threshold in degrees (uint32, rounded)
    ConditionAngleToTarget,
    //   ConditionShouldSuspend: Success if lcf::IS_SUSPENDED set; data=0
    ConditionShouldSuspend,
    //   ActionSuspendSelf: sets lcf::IS_SUSPENDED → BTSystem skips this entity; Success
    ActionSuspendSelf,
    //   ConditionTargetDistLOS: Success if dist(self,last_known)<=threshold AND activation[VISUAL]>=lo
    //     data = max_dist_m * 10 (uint32); uses WorldTransform pos + SenseComponent
    ConditionTargetDistLOS,

    // ── Batch 12 ──────────────────────────────────────────────────────────────
    //   ConditionTargetRoutingDistance: nav distance to target <= threshold.
    //     Sums cached path segment lengths; falls back to Euclidean when no path cached.
    //     data = max_dist_m * 10 (uint32, 1 decimal); uses WorldTransform + SenseComponent
    ConditionTargetRoutingDistance,
    //   ConditionAlienIsAllowed: checks AlienConfigPreset::allowed_actions for the active config.
    //     Success if bit (1 << uint8(action)) is set in DirectorSystem::GetActiveConfig() preset.
    //     data = AlienActionType value (uint8)
    ConditionAlienIsAllowed,
    //   DecoratorLockVent: acquires exclusive vent slot on entry; releases on child return.
    //     data = vent_id & 0x7 (0-7); Failure immediately if another entity holds the slot.
    DecoratorLockVent,

    // ── Batch 16 ──────────────────────────────────────────────────────────────
    //   SequenceIgnoreChildFail: like Sequence but skips Failure children instead
    //     of propagating Failure upward. Returns Success when all children visited.
    //     data=0; found in MD ALIEN_BEHAVE.XML ChildStateType="IGNORE_CHILD_FAIL:1"
    SequenceIgnoreChildFail,

    // ── Batch 15: HIGH-priority patterns from MD engine analysis ──────────────
    //   ConditionIsEnemyOfTarget: reads target from bb["target_entity"], compares
    //     self.alliance_group vs target.alliance_group via AllianceMatrix::IsEnemy.
    //     Failure if no bb/target/AgentState on either entity. data=0
    ConditionIsEnemyOfTarget,
    //   ActionForceIdle: always returns Running — forces entity to skip this tick.
    //     Equivalent to MD ActionForceIdle/ActionIdle. data=0
    ActionForceIdle,
    //   ConditionHasValidRouteToTarget: Success if PathCache has a cached path from
    //     self WorldTransform pos to target WorldTransform pos (PosKey round-trip).
    //     Failure if no bb/target/WorldTransform or cache miss. data=0
    ConditionHasValidRouteToTarget,

    // ── Batch 17 ──────────────────────────────────────────────────────────────
    //   ConditionIsCharacterClass: Success if as->character_class == (CharacterClass)data
    ConditionIsCharacterClass,
    //   ConditionIsInCover: Success if lcf::IS_IN_COVER is set; data=0
    ConditionIsInCover,
    //   ConditionShouldUseCover: Success if aggro_level >= Warning (3); data=0
    ConditionShouldUseCover,
    //   ActionMoveToCover: sets ff::SHOULD_MOVE_TO_COVER in frame_flags; always Success
    ActionMoveToCover,
    //   ActionIdleInCover: sets lcf::IS_IN_COVER + combat_state=EnteredCover; always Running
    ActionIdleInCover,

    // ── Batch 18 ──────────────────────────────────────────────────────────────
    //   BehaviourMoodSetCheck: Success if as->behaviour_mood_set == (BehaviourMoodSet)data
    //     data = BehaviourMoodSet value (uint8_t)
    BehaviourMoodSetCheck,
    //   SetBehaviourMoodSet: as->behaviour_mood_set = (BehaviourMoodSet)data; always Success
    SetBehaviourMoodSet,
    //   ViewconeTypeCheck: Success if as->viewcone_type == (ViewconeType)data
    //     data = ViewconeType value (uint8_t)
    ViewconeTypeCheck,
    //   SetViewconeType: as->viewcone_type = (ViewconeType)data; always Success
    SetViewconeType,
    //   SensoryTypeCheck: Success if as->last_sensory_type == (SensoryType)data
    //     data = SensoryType value (uint8_t)
    SensoryTypeCheck,
    //   SetSensoryType: as->last_sensory_type = (SensoryType)data; always Success
    SetSensoryType,

    // ── Batch 19 ──────────────────────────────────────────────────────────────
    // Weapon system — requires WeaponComponent on entity.
    //   ConditionCurrentWeaponIsEquipped: Success if wc->is_equipped; data=0
    ConditionCurrentWeaponIsEquipped,
    //   ConditionCurrentWeaponNeedsReloading: Success if wc->needs_reload; data=0
    ConditionCurrentWeaponNeedsReloading,
    //   ConditionHasMeleeAttackAvailable: Success if wc->melee_available; data=0
    ConditionHasMeleeAttackAvailable,
    //   ActionWeaponEquip: sets wc->is_equipped = true; always Success; no-op if no WeaponComponent
    ActionWeaponEquip,
    //   ActionRangedShoot: sets ff::SHOULD_RANGED_SHOOT in frame_flags; always Success
    ActionRangedShoot,
    //   ConditionHasObjective: Success if bb["has_objective"] == true; data=0
    ConditionHasObjective,
    //   ConditionBehaviourMoodSetAbove: Success if as->behaviour_mood_set > (BehaviourMoodSet)data
    //     data = BehaviourMoodSet threshold (uint8_t)
    ConditionBehaviourMoodSetAbove,

    // ── Batch 20 ──────────────────────────────────────────────────────────────
    //   ActionSuccess: always returns BTStatus::Success; data=0
    ActionSuccess,
    //   ActionFail: always returns BTStatus::Failure; data=0
    ActionFail,
    //   DecoratorTimer: run child for AT MOST duration_ms; timer expires → Success
    //     data=(timer_id<<24)|duration_ms; starts timer once on first entry; clear on exit
    DecoratorTimer,
    //   DecoratorThrottle: rate-limit subtree execution — child runs at most once per interval_ms.
    //     BehaviorTree.CPP: setTickFrequency(interval_ms). Useful for expensive checks (sense,
    //     path queries) at reduced frequency without a full LOD tier change.
    //     data=(timer_id<<24)|interval_ms; on entry: if timer not expired → return last result;
    //     else run child, store result, reset timer. Default: returns Running while throttled.
    //     RE source: AI.exe "AI_CHARACTER_LOCO[16]" pattern — per-node update budgets.
    DecoratorThrottle,
    //   ActionIdle: always returns BTStatus::Running (blocks forever); data=0
    ActionIdle,
    //   ConditionTargetIsWithinDistance: Euclidean dist(self,last_known_x/z) <= max_dist
    //     data = max_dist_m * 10 (uint32, 1 decimal); uses WorldTransform + SenseComponent
    ConditionTargetIsWithinDistance,
    //   ConditionHasAWeapon: Success if wc->is_equipped OR wc->weapon_type != 0; data=0
    ConditionHasAWeapon,
    //   ConditionShouldProcessSuspiciousItem: Success if nm->event_count > 0; data=0
    ConditionShouldProcessSuspiciousItem,

    // ── Batch 21 ──────────────────────────────────────────────────────────────
    //   ActionMoveToTarget: sets ff::SHOULD_MOVE_TO_TARGET + writes target_speed; always Success
    //     data = LocomotionTargetSpeed (0=Slowest,1=Slow,2=Fast,3=Fastest)
    ActionMoveToTarget,
    //   ActionMakeAggressive: aggro_level=NoLimit + sets ff::SHOULD_MOVE_TO_TARGET; always Success
    //     data = 0 (unused)
    ActionMakeAggressive,
    //   ConditionAllowedToAttackTarget: aggro_level>=Warning AND IsEnemy(self,target); data=0
    ConditionAllowedToAttackTarget,
    //   ActionDead: sets lcf::IS_DEAD + ff::SHOULD_PLAY_DEATH; always Success; data=0
    ActionDead,
    //   ActionMeleeAttack: sets ff::SHOULD_MELEE_ATTACK; always Success
    //     data = attack_type (0=Any,1=Light,2=Heavy,3=Special)
    ActionMeleeAttack,
    //   ConditionIsCurrentCoverValid: Success if lcf::IS_IN_COVER is set; data=0
    ConditionIsCurrentCoverValid,

    // ── Batch 22 ──────────────────────────────────────────────────────────────
    //   ConditionLastTimeSensed: Success if now_ms - sc->last_activated_ms[idx] <= max_ms
    //     data = (sense_idx<<24)|max_elapsed_ms (24-bit ms, same as SenseTimeCheck)
    ConditionLastTimeSensed,
    //   ActionPerformRole: atomically claims role slot (query_id=0); Success/Failure
    //     data = NpcRole value (uint8_t)
    ActionPerformRole,
    //   ConditionWasSenseThresholdLastIncreaseActivationAbove:
    //     Success if sc->activation[sense_idx] >= qualifier threshold
    //     data = (sense_idx<<8)|SenseThresholdQualifier
    ConditionWasSenseThresholdLastIncreaseActivationAbove,
    //   ConditionTargetsWeaponHasAmmo: Success if target's weapon !needs_reload
    //     data = 0; target from bb["target_entity"]
    ConditionTargetsWeaponHasAmmo,
    //   ConditionTargetsWeaponHasProperty: Success if target's weapon_type == data
    //     data = weapon_type (uint8_t)
    ConditionTargetsWeaponHasProperty,
    //   ConditionHasSearchedMostRecentSensedPosition:
    //     Success if lcf::HAS_SEARCHED_RECENT_SENSED_POS set; data=0
    ConditionHasSearchedMostRecentSensedPosition,

    // ── Batch 23 ──────────────────────────────────────────────────────────────
    //   ActionIdleTimeFacingTarget: wait duration_ms facing target; sets ff::SHOULD_FACE_TARGET while Running
    //     data = duration_ms (same layout as ActionIdleTime/Wait)
    ActionIdleTimeFacingTarget,
    //   ActionIdleTimeFacingTargetMostRecentSensedPosition: same timer + ff::SHOULD_FACE_LAST_KNOWN_POS
    //     data = duration_ms
    ActionIdleTimeFacingTargetMostRecentSensedPosition,
    //   ActionIdleTimeFacingSuspiciousItem: same timer + ff::SHOULD_FACE_SI_POS
    //     data = duration_ms
    ActionIdleTimeFacingSuspiciousItem,
    //   ActionRangedAim: sets ff::SHOULD_RANGED_AIM; always Running (goto exit_loop)
    //     data = 0 (unused)
    ActionRangedAim,
    //   ActionSuspiciousItemReaction: sets as->si_reaction + ff::SI_REACTION_SET; always Success
    //     data = SuspiciousItemReaction value (uint8_t)
    ActionSuspiciousItemReaction,
    //   ActionSuspectTargetResponse: sets ff::SHOULD_DO_SUSPECT_TARGET_RESPONSE; always Success
    //     data = 0 (unused)
    ActionSuspectTargetResponse,
    // ── Batch 24 ──────────────────────────────────────────────────────────────
    //   ActionRequestCover: sets ff::REQUESTING_COVER; always Running (wait for cover)
    //     data = 0 (unused)
    ActionRequestCover,
    //   ConditionHasValidCoverToChangeTo: Success if !lcf::IS_IN_COVER (can seek new cover)
    //     data = 0 (unused)
    ConditionHasValidCoverToChangeTo,
    // ── Batch 25 ──────────────────────────────────────────────────────────────
    //   ConditionHasScript: Success if entity has LuaScriptComponent with script_id != 0
    //     data = 0 (unused)
    ConditionHasScript,
    //   ActionScript: calls LuaSystem::CallAction(name, e) via BTLuaScriptRegistry
    //     data = fnv1a(script_name)
    ActionScript,
    // ── Batch 26 ──────────────────────────────────────────────────────────────
    //   ConditionLastTimeSearchedWithinTime: Success if last_searched_ms set within data ms
    //     data = max_elapsed_ms (uint32)
    ConditionLastTimeSearchedWithinTime,
    //   ConditionAllowedToSearch: Success if awareness >= Suspicious && !IS_DEAD
    //     data = 0 (unused)
    ConditionAllowedToSearch,
    //   ConditionHasDoneSuspectResponseMoveTo: Success if lcf::IS_DONE_SUSPECT_RESPONSE_MOVETO set
    //     data = 0 (unused)
    ConditionHasDoneSuspectResponseMoveTo,
    //   ConditionHasDoneSuspectResponseWithinTime: Success if event_ts[SuspectTargetResponse] within data ms
    //     data = max_elapsed_ms (uint32)
    ConditionHasDoneSuspectResponseWithinTime,
    //   ConditionHasKilltrap: Success if lcf::HAS_KILLTRAP set
    //     data = 0 (unused)
    ConditionHasKilltrap,
    //   ConditionHasMeleeAttackAvailableOrIsAttacking: Success if wc.melee_available OR ff::SHOULD_MELEE_ATTACK
    //     data = 0 (unused)
    ConditionHasMeleeAttackAvailableOrIsAttacking,
    //   ConditionIsBranchActive: Success if NamedBranchRegistry::IsActive(data)
    //     data = fnv1a(branch_name)
    ConditionIsBranchActive,
    //   ConditionIsRequestingCover: Success if ff::REQUESTING_COVER set this frame
    //     data = 0 (unused)
    ConditionIsRequestingCover,
    //   ConditionAllowedToPursueTarget: Success if aggro_level >= NpcAggroLevel::Warning
    //     data = 0 (unused)
    ConditionAllowedToPursueTarget,
    // ── Batch 27 ──────────────────────────────────────────────────────────────
    //   ActionBreakout: sets ff::SHOULD_BREAKOUT; always Running
    //     data = 0 (unused)
    ActionBreakout,
    //   ActionMoveToMostRecentSensedPosition: sets ff::SHOULD_MOVE_TO_LAST_KNOWN; always Running
    //     data = 0 (unused)
    ActionMoveToMostRecentSensedPosition,
    //   ActionMoveToNearestStandingPointToTarget: sets ff::SHOULD_MOVE_NEAR_TARGET; always Running
    //     data = 0 (unused)
    ActionMoveToNearestStandingPointToTarget,
    //   ActionMoveToAttackTarget: sets ff::SHOULD_ATTACK_MOVE; always Running
    //     data = 0 (unused)
    ActionMoveToAttackTarget,
    //   ActionChangeCover: sets ff::SHOULD_CHANGE_COVER; always Running
    //     data = 0 (unused)
    ActionChangeCover,
    // ── Batch 28 ──────────────────────────────────────────────────────────────
    //   ConditionIsInTargetsWeaponRange: dist(self→last_known) <= data*0.1m
    //     data = max_dist_m * 10 (uint32, 1 decimal place)
    ConditionIsInTargetsWeaponRange,
    //   ConditionIsCoverExposed: IS_IN_COVER && activation[VISUAL] >= threshold_hi
    //     data = 0 (unused)
    ConditionIsCoverExposed,
    //   ConditionHasLostTarget: now_ms - last_activated_ms[VISUAL] > data; never-seen → Failure
    //     data = max_elapsed_ms (uint32)
    ConditionHasLostTarget,
    //   ActionUpdateLastKnownPosition: sets ff::SHOULD_UPDATE_LAST_KNOWN; always Running
    //     data = 0 (unused)
    ActionUpdateLastKnownPosition,
    //   ActionRequestInvestigate: sets ff::SHOULD_INVESTIGATE; always Running
    //     data = 0 (unused)
    ActionRequestInvestigate,
    //   ActionMarkTargetLost: sets ff::SHOULD_MARK_TARGET_LOST; always Running
    //     data = 0 (unused)
    ActionMarkTargetLost,
    //   ActionForceRetreat: sets ff::SHOULD_FORCE_RETREAT; always Running
    //     data = 0 (unused)
    ActionForceRetreat,
    //   ActionHoldPosition: sets ff::SHOULD_HOLD_POSITION; always Running
    //     data = 0 (unused)
    ActionHoldPosition,
    // ── Batch 29 ──────────────────────────────────────────────────────────────
    //   ConditionNpcDevelopmentStageAbove: nc->stage > NpcDevelopmentStage(data)
    //     data = uint32_t(NpcDevelopmentStage value) e.g. 0=Naive, 2=ThreatAware
    ConditionNpcDevelopmentStageAbove,
    //   ConditionNpcHasAbility: nc->HasAbility(uint16_t(data))
    //     data = npc_ability::* bitmask value (uint16_t)
    ConditionNpcHasAbility,
    //   ConditionIsHostileToPlayer: AllianceMatrix::IsEnemy(self.alliance_group, AllianceGroup::Player)
    //     data = 0 (unused); Failure if no AgentState
    ConditionIsHostileToPlayer,
    //   ActionPerformAmbush: sets ff::SHOULD_PERFORM_AMBUSH; always Running
    //     data = 0 (unused)
    ActionPerformAmbush,
    //   ActionStartSearch: sets ff::SHOULD_START_SEARCH; always Running
    //     data = 0 (unused)
    ActionStartSearch,
    //   ActionCallForHelp: sets ff::SHOULD_CALL_FOR_HELP; always Running
    //     data = 0 (unused)
    ActionCallForHelp,
    //   ActionTauntTarget: sets ff::SHOULD_TAUNT_TARGET; always Running
    //     data = 0 (unused)
    ActionTauntTarget,
    //   ActionSurrenderSelf: sets ff::SHOULD_SURRENDER; always Running
    //     data = 0 (unused)
    ActionSurrenderSelf,
    // ── Batch 30 ──────────────────────────────────────────────────────────────
    //   ConditionEventCountAbove: nm->event_count > data
    //     data = min_count (uint8_t); Failure if no NpcMemoryComponent
    ConditionEventCountAbove,
    //   ConditionSpatialMemoryCountAbove: nm->spatial_count > data
    //     data = min_count (uint8_t); Failure if no NpcMemoryComponent
    ConditionSpatialMemoryCountAbove,
    //   ConditionMotivationTicksAbove: as->motivation_ticks > data
    //     data = min_ticks (uint8_t); Failure if no AgentState
    ConditionMotivationTicksAbove,
    //   ConditionHasVisualHistory: sc->last_activated_ms[0] > 0 (ever spotted)
    //     data = 0 (unused); Failure if no SenseComponent
    ConditionHasVisualHistory,
    //   ActionPursueTarget: sets ff::SHOULD_PURSUE_TARGET; always Running
    //     data = 0 (unused)
    ActionPursueTarget,
    //   ActionCircleTarget: sets ff::SHOULD_CIRCLE_TARGET; always Running
    //     data = 0 (unused)
    ActionCircleTarget,
    //   ActionBackOff: sets ff::SHOULD_BACK_OFF; always Running
    //     data = 0 (unused)
    ActionBackOff,
    //   ActionCrouchMove: sets ff::SHOULD_CROUCH_MOVE; always Running
    //     data = 0 (unused)
    ActionCrouchMove,
    //   ActionVault: sets ff::SHOULD_VAULT; always Running
    //     data = 0 (unused)
    ActionVault,

    // ── Batch 31: VentRegistry conditions ────────────────────────────────────
    //   ConditionHasVentCloseToAlien: data = radius_m*10 (default 60=6m)
    //     VentRegistry::Get().AnyWithinRadius(self.x, self.z, radius_m)
    ConditionHasVentCloseToAlien,
    //   ConditionHasFlankedVentCloseToPlayer: data = radius_m*10 (default 60=6m)
    //     uses sc->last_known_x/z (player last seen pos); Failure if never seen
    ConditionHasFlankedVentCloseToPlayer,
    //   ConditionTargetIsOnlyAccessibleCrouching: data = 0
    //     reads target from bb["target_entity"]; Success if target has lcf::IS_IN_VENT
    ConditionTargetIsOnlyAccessibleCrouching,
    //   ConditionAngleNPCToTargetsAimLessThan: data = angle_deg (uint8)
    //     reads target from bb["target_entity"]; checks if target's forward (rot_y)
    //     is within angle_deg of direction (target→self); Success when target faces NPC
    ConditionAngleNPCToTargetsAimLessThan,

    // ── Batch 32: TokenRegistry ───────────────────────────────────────────────
    //   ConditionHasToken: data = token_id (FNV-1a of token name string)
    //     MdTokenRegistry::AcquireToken(data, e) → Success if slot available
    ConditionHasToken,
    //   ActionReleaseToken: data = token_id (FNV-1a of token name string)
    //     MdTokenRegistry::ReleaseToken(data, e); always Success
    ActionReleaseToken,

    // ── Batch 33: RE HIGH-priority nodes (Kenshi/AI:Isolation combat) ────────
    //   ActionAbortMeleeAttack:  data=0; sets ff::SHOULD_ABORT_MELEE; Running
    ActionAbortMeleeAttack,
    //   ActionGetOutOfTheWay:    data=0; sets ff::SHOULD_GET_OUT_OF_WAY; Running
    ActionGetOutOfTheWay,
    //   ActionHitTargetAndRun:   data=0; sets lcf::SHOULD_HIT_AND_RUN; Running
    ActionHitTargetAndRun,
    //   ActionMoveInDirection:   data=0(toward),1(away),2(left),3(right);
    //     sets ff::SHOULD_MOVE_IN_DIRECTION + as->move_direction=data; Running
    ActionMoveInDirection,
    //   ActionSuspend:           data=0; sets lcf::IS_SUSPENDED; Success
    //     (BTSystem::Tick skips entity while IS_SUSPENDED is set)
    ActionSuspend,
    //   ActionTakeStep:          data=0; sets ff::SHOULD_TAKE_STEP; Running
    ActionTakeStep,
    //   ActionThreatAware:       data=0; sets ff::SHOULD_THREAT_AWARE + lcf::DOING_THREAT_ANIM; Running
    ActionThreatAware,
    //   ActionThreatEscalation:  data=0; sets ff::SHOULD_THREAT_ESCALATE; increments aggro_level; Running
    ActionThreatEscalation,
    //   ConditionCanShootNow:    data=0; Success if wc->is_equipped && !wc->needs_reload
    //     && bb["target_entity"] valid && sc->activation[0] >= 0.5f
    ConditionCanShootNow,
    //   ConditionCheckHealthState: data=threshold_pct (uint8, 0-100);
    //     Success if as->hp_pct <= threshold_pct (for "I need healing/retreat" checks)
    ConditionCheckHealthState,
    //   ConditionHasGroupAwarenessState: data=AwarenessState (uint8);
    //     Success if as->awareness >= AwarenessState(data) (individual proxy for group)
    ConditionHasGroupAwarenessState,
    //   ConditionHasMeleeBlockAvailable: data=0;
    //     Success if wc->melee_available && as->combat_state == Blocking
    ConditionHasMeleeBlockAvailable,
    //   ConditionHasMeleeCounterAttackAvailable: data=0;
    //     Success if wc->melee_available && target has ff::SHOULD_MELEE_ATTACK set
    ConditionHasMeleeCounterAttackAvailable,
    //   ConditionLastTimeTargetShotAtMe: data=max_elapsed_ms (uint32);
    //     Success if (nowMs - as->last_shot_at_ms) <= data && last_shot_at_ms != 0
    ConditionLastTimeTargetShotAtMe,
    //   ConditionTargetIsInWeaponRange: data=0;
    //     reads target from bb["target_entity"]; Success if dist <= target->wc.attack_range (or 1.5m)
    ConditionTargetIsInWeaponRange,
    //   ConditionTargetIsTargetingMe: data=0;
    //     Success if target has lcf::IS_TARGETED set on self (CombatSystem sets this)
    ConditionTargetIsTargetingMe,
    //   ConditionTargetIsUsingMeleeAttack: data=0;
    //     Success if target has ff::SHOULD_MELEE_ATTACK set this tick
    ConditionTargetIsUsingMeleeAttack,
    //   DecoratorLoop: data=N (0=infinite); repeats child until N successes or child Failure
    //     Uses st.counter to track iteration count
    DecoratorLoop,

    // ── Batch 34: RE MEDIUM-priority nodes ───────────────────────────────────
    //   ActionForceSearch:      data=0; ff::SHOULD_FORCE_SEARCH; Running
    ActionForceSearch,
    //   ActionResetSearchJobs:  data=0; clears HAS_SEARCHED_RECENT_SENSED_POS + last_searched_ms=0; Success
    ActionResetSearchJobs,
    //   ActionSetFrameFlag:     data=bit_idx (ff::*); sets that frame flag bit; Success
    ActionSetFrameFlag,
    //   ActionSetGaugeAmount:   data=(GaugeType<<16)|(uint16)(value*1000); sets gauge; Success
    ActionSetGaugeAmount,
    //   ConditionHasSenseActivationBeenAbove: data=(sense_idx<<8)|SenseThresholdQualifier
    //     Success if sc->last_activated_ms[sense_idx]!=0 AND activation[idx] >= qualifier
    ConditionHasSenseActivationBeenAbove,
    //   ConditionIsCoverTooClose: data=min_dist_m*10; Success if IS_IN_COVER && dist(self,target)<=min_dist
    ConditionIsCoverTooClose,
    //   ConditionIsInCombatArea: data=0; Success if as->awareness >= Aware (combat engagement proxy)
    ConditionIsInCombatArea,
    //   ConditionMostRecentSenseActivationHasBeenAbove: data=SenseThresholdQualifier
    //     finds sense with most recent last_activated_ms; Success if its activation >= qualifier
    ConditionMostRecentSenseActivationHasBeenAbove,
    //   ConditionNeedsToGetOutOfTheWay: data=0; Success if ff::SHOULD_GET_OUT_OF_WAY is set
    ConditionNeedsToGetOutOfTheWay,
    //   ConditionObjectiveIsInCombatArea: data=0; Success if bb["squad_activity"] >= AttackMove(9)
    ConditionObjectiveIsInCombatArea,
    //   ConditionObjectiveIsWithinDistance: data=max_dist_m*10
    //     reads bb["squad_tx"/"squad_tz"]; Success if dist(self, objective) <= max_dist
    ConditionObjectiveIsWithinDistance,
    //   ConditionShouldFollow: data=0; Success if bb["squad_activity"] >= AttackMove(9)
    ConditionShouldFollow,
    //   ConditionTargetIsInCombatArea: data=0; Success if target as->awareness >= Aware
    ConditionTargetIsInCombatArea,
    //   ConditionTargetIsWithinAggroRadius: data=radius_m*10; dist(self,target)<=radius — Success
    ConditionTargetIsWithinAggroRadius,
    //   ConditionTargetNearestStandPointIsWithinDistance: data=max_dist_m*10
    //     Euclidean dist + 1m buffer (stand-point offset); approximation of nav standing-point
    ConditionTargetNearestStandPointIsWithinDistance,
    //   DecoratorSetSenseSet: data=sense_set_id; no-op passthrough (sense sets not implemented)
    DecoratorSetSenseSet,
    //   DecoratorSuspiciousItemInProgress: gate child on nm->event_count > 0
    DecoratorSuspiciousItemInProgress,
    //   SelectorLinear: standard left-to-right selector (same semantics as Selector)
    SelectorLinear,
    //   SequenceLinear: stateful sequence — remembers child position across ticks
    //     (unlike SequenceStateless which resets to child 0 every entry)
    SequenceLinear,

    // ── Batch 35: remaining LOW-priority nodes ────────────────────────────────
    //   ActionApplyPrimaryDamageControlResponse: data=0; ff::SHOULD_APPLY_DAMAGE_CONTROL; Running
    ActionApplyPrimaryDamageControlResponse,
    //   ActionBrokenCover: data=0; clear lcf::IS_IN_COVER + ff::SHOULD_BREAKOUT; Success
    ActionBrokenCover,
    //   ActionMoveToObjective: data=0; reads bb["squad_tx/tz"] → NavAgent target + ff::SHOULD_MOVE_TO_OBJECTIVE; Running
    ActionMoveToObjective,
    //   ActionSetLogicCharacterFlags: data=(bit_idx<<8)|(1=set/0=clear); modifies lcf bit; Success
    ActionSetLogicCharacterFlags,
    //   ConditionAllowedToDoSuspiciousWarning: data=0; aggro_level>=Warning && awareness>=Suspicious; Success
    ConditionAllowedToDoSuspiciousWarning,
    //   ConditionCanTakeStep: data=0; awareness>=Suspicious && !IS_DEAD && target in bb; Success
    ConditionCanTakeStep,
    //   ConditionGameIsDifficulty: data=0; always Success (no difficulty system)
    ConditionGameIsDifficulty,
    //   ConditionHasAnySenseBeenAboveWithinTime: data=(qualifier<<24)|(time_ms 24-bit)
    //     Success if any sense last_activated_ms within time_ms AND activation>=qualifier
    ConditionHasAnySenseBeenAboveWithinTime,
    //   ConditionIsCorpseTrap: data=0; Success if lcf::IS_CORPSE_TRAP is set
    ConditionIsCorpseTrap,
    //   ConditionIsGaugeAmountBelow: data=(GaugeType<<24)|(threshold*1000 24-bit); gauge < threshold → Success
    ConditionIsGaugeAmountBelow,
    //   ConditionLastTimeWasAbleToShootTarget: data=max_elapsed_ms
    //     Success if sc->last_activated_ms[0]!=0 && (nowMs-last_activated_ms[0])<=data
    ConditionLastTimeWasAbleToShootTarget,
    //   ConditionRequiresPrimaryDamageControlResponse: data=threshold_pct (uint8);
    //     Success if as->hp_pct <= threshold (critical HP — needs first aid)
    ConditionRequiresPrimaryDamageControlResponse,
    //   ConditionTargetIsPlayer: data=0; reads target from bb; Success if target lcf::IS_PLAYER
    ConditionTargetIsPlayer,
    //   ConditionTargetIsWithinDistanceUnobscured: data=max_dist_m*10;
    //     dist(self,target)<=max_dist AND sc->activation[0]>=threshold_lo
    ConditionTargetIsWithinDistanceUnobscured,
    //   ConditionTargetLogicCharacterFlags: data=(bit_idx<<8)|(check_set:1/check_clear:0)
    //     reads target from bb; checks target's lcf bit
    ConditionTargetLogicCharacterFlags,
};

// Leaf functions accept engine context; game side casts to GameState& (which inherits EngineContext)
using BTConditionFunc = bool    (*)(md::EngineContext&, entt::entity);
using BTActionFunc    = BTStatus(*)(md::EngineContext&, entt::entity);

// BTNode: 24 bytes, cache-line friendly, flat array — no heap
// data encoding per node type:
//   TimerStart:       (timer_id << 24) | duration_ms (max 16.7 s per slot)
//   TimerCheck:       timer_id & 0x1Fu  (AgentTimerSlot value)
//   FlagCheck/Set:    bit_idx (0-63, maps to lcf::* constants)
//   SenseCheck:       (sense_idx << 24) | (threshold * 1000)
//   MotivationCheck:  MotivationType value (uint8_t)
//   SetMotivation:    MotivationType value (uint8_t)
//   Reference:        unused (BehaviorTree* in _padding)
//   GaugeCheck:       (GaugeType << 24) | (threshold * 1000)
//   GaugeSet:         (GaugeType << 24) | (value * 1000)
//   Branch:           BranchType in lower byte (semantic only)
//   FrameFlagCheck/Set: bit_idx (0-63) → frame_flags field of AgentState
//   WeightedSelector: bits 0-7=w[0], 8-15=w[1], 16-23=w[2], 24-31=w[3]
//   AwarenessCheck:   AwarenessState value
//   AlertnessCheck:   AlertnessState value
//   MoodCheck:        NpcMood value
//   RoleCheck:        (NpcRole << 8) | mode (0=performing, 1=could_perform)
//   RoleClaim:        (query_id << 8) | (NpcRole & 0xFF)
//   RoleRelease:      NpcRole value
//   WithdrawCheck:    WithdrawState value
//   SetWithdraw:      WithdrawState value
//   MemoryCheck:      0=has_spatial_memory, 1=has_event_memory
//   AreaSweepCheck:      AreaSweepType value (uint8_t cast from AgentState::area_sweep_type)
//   DecoratorPercentage: percentage 0-100 (child runs if LCG % 100 < data)
//   SelectorPercentage:  unused (child count determines range)
//   SenseTimeCheck:      (sense_idx << 24) | max_elapsed_ms (24 bits)
//   AggroLevelCheck:     NpcAggroLevel value (uint8_t)
//   SetAggroLevel:       NpcAggroLevel value (uint8_t)
//   NpcCombatStateCheck: NpcCombatState value (uint8_t)
//   SetNpcCombatState:   NpcCombatState value (uint8_t)
//   DecoratorMood:       NpcMood value (uint8_t)
//   DecoratorAwareness:  AwarenessState value (uint8_t)
//   DecoratorTimerAuto:  (timer_id<<24)|duration_ms (24-bit ms)
//   ActionTimerRandom:   (timer_id<<24)|(max_u12<<12)|min_u12; unit=100ms each (max 409.5s)
//   ActionSquadNotify:   SquadSignal value (uint8_t)
//   ConditionSquadSignal:SquadSignal value (uint8_t)
//   ConditionAnySenseWithinTime: (sense_idx<<28)|time_window_ms (28-bit ms ~3 days)
//   ActionExpireTimer:   timer_id & 0x1F
//   TargetFlagCheck:     bit_idx & 0x3F; reads target from bb key fnv1a("target_entity")
//   SetLocomotionState:  LocomotionState value (uint8_t)
//   ActionIdleTime:      duration_ms (uses st.timer, same as Wait)
//   ConditionHaveTarget/HaveNextTarget: 0 (no param)
//   ActionTriggerSound:  NpcSoundEvent (uint8)
//   ConditionIsSenseActivationAbove: (sense_idx<<8)|SenseThresholdQualifier
//   ConditionHasAnySenseBeenAbove:   SenseThresholdQualifier (uint8)
//   ActionSuspiciousItemDoneStage:   SuspiciousItemStage (uint8)
//   ConditionSuspiciousItemBTPriority: min_intensity (0=low,1=med,2=high)
//   ConditionLastTimeSquadNotified:  (uint8_t(signal)<<8)|time_threshold_s
//   DecoratorAggressionEscalation:  0 (no param)
//   ConditionEventAOccuredAfterB: (EventType_A<<8)|EventType_B (4-value enum each)
//   ConditionSuspiciousItemShouldDoStage: SuspiciousItemStage value (uint8_t)
//   ConditionSuspiciousItemIsWithinDistance: max_dist_cm (uint32, metres*100)
//   SIReactionCheck:  SuspiciousItemReaction value (uint8_t)
//   SetSIReaction:    SuspiciousItemReaction value (uint8_t)
//   AmbushTypeCheck:  AmbushType value (uint8_t)
//   SetAmbushType:    AmbushType value (uint8_t)
//   NoiseTypeCheck:   NoiseType value (uint8_t)
//   SetNoiseType:     NoiseType value (uint8_t)
//   ConditionSuspiciousItemValid: 0 (unused)
//   ActionConsumeSuspiciousItem:  0 (unused)
//   ActionForceMoveToSI:          0 (unused; game reads nm->events[0].x/z via ff flag)
//   RelationshipTrustCheck: (threshold<<8)|mode; mode=0→trust>=thr, 1→trust<thr
//   RelationshipFearCheck:  (threshold<<8)|mode; mode=0→fear>=thr,  1→fear<thr
//   ConditionIsInVent:      data=char_type; 0=Owner, 1=Target(bb), 2=Both
//   ConditionIsAnySenseActivationAbove: SenseThresholdQualifier (uint8)
//   ActionMoveThroughTarget: LocomotionTargetSpeed (bits 0-1)
//   ActionAdjustMenace:      (delta_fixed<<2)|mode; delta_fixed=uint8(delta*10); mode:0=add,1=set,2=sub
//   ConditionAngleToTarget: angle threshold in degrees (uint32, rounded)
//   ConditionShouldSuspend: 0 (unused)
//   ActionSuspendSelf:      0 (unused)
//   ConditionTargetDistLOS: max_dist_m * 10 (uint32, 1 decimal place)
//   ConditionTargetRoutingDistance: max_dist_m * 10 (uint32, 1 decimal place)
//   ConditionAlienIsAllowed: AlienActionType value (uint8)
//   DecoratorLockVent: vent_id (0-7, 3 bits)
//   ConditionIsEnemyOfTarget:     0 (no param; reads alliance_group from AgentState)
//   ActionForceIdle:              0 (no param; always Running)
//   ConditionHasValidRouteToTarget: 0 (no param; PathCache::Get check)
//   ConditionIsCharacterClass: CharacterClass value (uint8_t)
//   ConditionIsInCover:        0 (checks lcf::IS_IN_COVER)
//   ConditionShouldUseCover:   0 (checks aggro_level >= Warning)
//   ActionMoveToCover:         0 (sets ff::SHOULD_MOVE_TO_COVER)
//   ActionIdleInCover:         0 (sets lcf::IS_IN_COVER + combat_state=EnteredCover)
//   BehaviourMoodSetCheck:    BehaviourMoodSet value (uint8_t)
//   SetBehaviourMoodSet:      BehaviourMoodSet value (uint8_t)
//   ViewconeTypeCheck:        ViewconeType value (uint8_t)
//   SetViewconeType:          ViewconeType value (uint8_t)
//   SensoryTypeCheck:         SensoryType value (uint8_t)
//   SetSensoryType:           SensoryType value (uint8_t)
//   ConditionCurrentWeaponIsEquipped:     0 (WeaponComponent::is_equipped)
//   ConditionCurrentWeaponNeedsReloading: 0 (WeaponComponent::needs_reload)
//   ConditionHasMeleeAttackAvailable:     0 (WeaponComponent::melee_available)
//   ActionWeaponEquip:                    0 (sets wc->is_equipped=true)
//   ActionRangedShoot:                    0 (sets ff::SHOULD_RANGED_SHOOT in frame_flags)
//   ConditionHasObjective:                0 (bb["has_objective"] bool)
//   ConditionBehaviourMoodSetAbove:       BehaviourMoodSet threshold (uint8_t; strict >)
//   ActionSuccess:                        0 (unused)
//   ActionFail:                           0 (unused)
//   DecoratorTimer:                       (timer_id<<24)|duration_ms (24-bit ms)
//   ActionIdle:                           0 (unused; always Running)
//   ConditionTargetIsWithinDistance:      max_dist_m * 10 (uint32, 1 decimal)
//   ConditionHasAWeapon:                  0 (is_equipped || weapon_type!=0)
//   ConditionShouldProcessSuspiciousItem: 0 (nm->event_count > 0)
//   ActionMoveToTarget:            LocomotionTargetSpeed (0-3)
//   ActionMakeAggressive:          0 (unused)
//   ConditionAllowedToAttackTarget:0 (unused; aggro+alliance check)
//   ActionDead:                    0 (unused)
//   ActionMeleeAttack:             attack_type uint8 (0=Any,1=Light,2=Heavy,3=Special)
//   ConditionIsCurrentCoverValid:  0 (checks lcf::IS_IN_COVER)
//   ConditionLastTimeSensed:       (sense_idx<<24)|max_elapsed_ms (24-bit ms)
//   ActionPerformRole:             NpcRole value (uint8_t)
//   ConditionWasSenseThresholdLastIncreaseActivationAbove: (sense_idx<<8)|SenseThresholdQualifier
//   ConditionTargetsWeaponHasAmmo: 0 (checks target WeaponComponent::needs_reload==false)
//   ConditionTargetsWeaponHasProperty: weapon_type (uint8_t)
//   ConditionHasSearchedMostRecentSensedPosition: 0 (lcf::HAS_SEARCHED_RECENT_SENSED_POS)
//   ActionIdleTimeFacingTarget:   duration_ms (same as ActionIdleTime)
//   ActionIdleTimeFacingTargetMostRecentSensedPosition: duration_ms
//   ActionIdleTimeFacingSuspiciousItem: duration_ms
//   ActionRangedAim:              0 (unused; always Running)
//   ActionSuspiciousItemReaction: SuspiciousItemReaction value (uint8_t)
//   ActionSuspectTargetResponse:  0 (unused)
//   ActionRequestCover:           0 (sets ff::REQUESTING_COVER; always Running)
//   ConditionHasValidCoverToChangeTo: 0 (!lcf::IS_IN_COVER → Success)
//   ConditionHasScript:           0 (LuaScriptComponent::script_id != 0 → Success)
//   ActionScript:                 fnv1a(script_name) → BTLuaScriptRegistry lookup → CallAction
//   ConditionLastTimeSearchedWithinTime: max_elapsed_ms (uint32)
//   ConditionAllowedToSearch:     0 (awareness>=Suspicious && !IS_DEAD)
//   ConditionHasDoneSuspectResponseMoveTo: 0 (lcf::IS_DONE_SUSPECT_RESPONSE_MOVETO)
//   ConditionHasDoneSuspectResponseWithinTime: max_elapsed_ms (uint32)
//   ConditionHasKilltrap:         0 (lcf::HAS_KILLTRAP)
//   ConditionHasMeleeAttackAvailableOrIsAttacking: 0
//   ConditionIsBranchActive:      fnv1a(branch_name)
//   ConditionIsRequestingCover:   0 (ff::REQUESTING_COVER)
//   ConditionAllowedToPursueTarget: 0 (aggro_level >= Warning)
//   ActionBreakout:               0 (sets ff::SHOULD_BREAKOUT; Running)
//   ActionMoveToMostRecentSensedPosition: 0 (sets ff::SHOULD_MOVE_TO_LAST_KNOWN; Running)
//   ActionMoveToNearestStandingPointToTarget: 0 (sets ff::SHOULD_MOVE_NEAR_TARGET; Running)
//   ActionMoveToAttackTarget:     0 (sets ff::SHOULD_ATTACK_MOVE; Running)
//   ActionChangeCover:            0 (sets ff::SHOULD_CHANGE_COVER; Running)
//   ConditionIsInTargetsWeaponRange: max_dist_m * 10 (dist ≤ threshold → Success)
//   ConditionIsCoverExposed:      0 (IS_IN_COVER && activation[0] >= threshold_hi)
//   ConditionHasLostTarget:       max_elapsed_ms (now - last_activated[0] > ms; 0-ts → Failure)
//   ActionUpdateLastKnownPosition: 0 (sets ff::SHOULD_UPDATE_LAST_KNOWN; Running)
//   ActionRequestInvestigate:     0 (sets ff::SHOULD_INVESTIGATE; Running)
//   ActionMarkTargetLost:         0 (sets ff::SHOULD_MARK_TARGET_LOST; Running)
//   ActionForceRetreat:           0 (sets ff::SHOULD_FORCE_RETREAT; Running)
//   ActionHoldPosition:           0 (sets ff::SHOULD_HOLD_POSITION; Running)
//   ConditionNpcDevelopmentStageAbove: uint32_t(NpcDevelopmentStage); nc->stage > NpcDevelopmentStage(data)
//   ConditionNpcHasAbility:       npc_ability::* bitmask (uint16_t); nc->HasAbility(data)
//   ConditionIsHostileToPlayer:   0 (AllianceMatrix::IsEnemy(self.alliance_group, Player))
//   ActionPerformAmbush:          0 (sets ff::SHOULD_PERFORM_AMBUSH; Running)
//   ActionStartSearch:            0 (sets ff::SHOULD_START_SEARCH; Running)
//   ActionCallForHelp:            0 (sets ff::SHOULD_CALL_FOR_HELP; Running)
//   ActionTauntTarget:            0 (sets ff::SHOULD_TAUNT_TARGET; Running)
//   ActionSurrenderSelf:          0 (sets ff::SHOULD_SURRENDER; Running)
//   ConditionEventCountAbove:     min_count uint8 (nm->event_count > data)
//   ConditionSpatialMemoryCountAbove: min_count uint8 (nm->spatial_count > data)
//   ConditionMotivationTicksAbove: min_ticks uint8 (as->motivation_ticks > data)
//   ConditionHasVisualHistory:    0 (sc->last_activated_ms[0] > 0)
//   ActionPursueTarget:           0 (sets ff::SHOULD_PURSUE_TARGET; Running)
//   ActionCircleTarget:           0 (sets ff::SHOULD_CIRCLE_TARGET; Running)
//   ActionBackOff:                0 (sets ff::SHOULD_BACK_OFF; Running)
//   ActionCrouchMove:             0 (sets ff::SHOULD_CROUCH_MOVE; Running)
//   ActionVault:                  0 (sets ff::SHOULD_VAULT; Running)
//   ConditionHasVentCloseToAlien:              data = radius_m*10; uses VentRegistry + WorldTransform
//   ConditionHasFlankedVentCloseToPlayer:      data = radius_m*10; uses VentRegistry + SenseComponent last_known
//   ConditionTargetIsOnlyAccessibleCrouching:  data = 0; reads target bb; checks lcf::IS_IN_VENT
//   ConditionAngleNPCToTargetsAimLessThan:     data = angle_deg(uint8); reads target bb; target's rot_y vs (target→self)
//   ConditionHasToken:    data = token_id (FNV-1a); MdTokenRegistry::AcquireToken → Success if slot free
//   ActionReleaseToken:   data = token_id (FNV-1a); MdTokenRegistry::ReleaseToken; always Success
//   ActionAbortMeleeAttack:  0 → ff::SHOULD_ABORT_MELEE; Running
//   ActionGetOutOfTheWay:    0 → ff::SHOULD_GET_OUT_OF_WAY; Running
//   ActionHitTargetAndRun:   0 → lcf::SHOULD_HIT_AND_RUN; Running
//   ActionMoveInDirection:   0=toward,1=away,2=left,3=right → ff::SHOULD_MOVE_IN_DIRECTION + as->move_direction
//   ActionSuspend:           0 → lcf::IS_SUSPENDED; Success
//   ActionTakeStep:          0 → ff::SHOULD_TAKE_STEP; Running
//   ActionThreatAware:       0 → ff::SHOULD_THREAT_AWARE + lcf::DOING_THREAT_ANIM; Running
//   ActionThreatEscalation:  0 → ff::SHOULD_THREAT_ESCALATE + aggro_level++; Running
//   ConditionCanShootNow:    0; wc equipped && !reload && target in bb && sc activation[0]>=0.5
//   ConditionCheckHealthState: threshold_pct uint8; as->hp_pct <= threshold → Success
//   ConditionHasGroupAwarenessState: AwarenessState uint8; as->awareness >= state → Success
//   ConditionHasMeleeBlockAvailable: 0; wc->melee_available && combat_state==Blocking
//   ConditionHasMeleeCounterAttackAvailable: 0; wc->melee_available && target ff::SHOULD_MELEE_ATTACK
//   ConditionLastTimeTargetShotAtMe: max_elapsed_ms uint32; nowMs-last_shot_at_ms <= data
//   ConditionTargetIsInWeaponRange: 0; dist(self, target) <= target wc.attack_range (or 1.5m)
//   ConditionTargetIsTargetingMe: 0; target has lcf::IS_TARGETED on self
//   ConditionTargetIsUsingMeleeAttack: 0; target ff::SHOULD_MELEE_ATTACK set
//   DecoratorLoop: N (0=infinite); loops child N times; st.counter tracks iterations
// flags encoding per node type:
//   FlagCheck:        0=check set, 1=check clear
//   FlagSet:          0=set bit, 1=clear bit
//   FrameFlagCheck:   0=check set, 1=check clear
//   FrameFlagSet:     0=set bit, 1=clear bit
//   TimerStart:       bit 0 = only_increase (C12: don't shorten existing timer)
//   DecoratorTimerAuto: bit 0 = only_increase (same semantics)
//   ConditionAnySenseWithinTime: 0=any sense, 1=specific sense (idx in data bits 28-31)
//   TargetFlagCheck:  0=check set, 1=check clear
//   Branch:           ShutdownSpeed (upper nibble, currently unused in VM)
struct BTNode {
    BTNodeType type;
    uint8_t    flags;
    uint16_t   parent;
    uint16_t   childStart;
    uint16_t   childCount;
    uint32_t   data;
    union {
        BTConditionFunc condition;
        BTActionFunc    action;
        void*           _padding;
    };
};
static_assert(sizeof(BTNode) == 24, "BTNode must be 24 bytes");

struct BTState {
    uint16_t currentChild;
    uint16_t counter;
    uint32_t timer;
};
static_assert(sizeof(BTState) == 8, "BTState must be 8 bytes");

class BehaviorTree {
public:
    static constexpr uint16_t INVALID      = 0xFFFF;
    static constexpr uint16_t MAX_NODES    = 128;
    static constexpr uint16_t MAX_CHILDREN = 256;

    BehaviorTree();

    uint16_t addSelector ();
    uint16_t addSequence ();
    uint16_t addInverter ();
    uint16_t addRepeat   (uint32_t count);  // 0 = infinite
    uint16_t addWait     (uint32_t ms);
    uint16_t addCondition(BTConditionFunc func);
    uint16_t addAction   (BTActionFunc    func);
    // M21 extensions
    uint16_t addBranch    (BTConditionFunc cond,
                           BranchType btype    = BranchType::Standard,
                           ShutdownSpeed speed = ShutdownSpeed::Graceful);
    uint16_t addTimerStart(uint8_t timer_id, uint32_t duration_ms);
    uint16_t addTimerCheck(uint8_t timer_id);
    // Pattern 4: bit_idx = lcf::* constant (0-63)
    uint16_t addFlagCheck (uint8_t bit_idx, bool check_set = true);
    uint16_t addFlagSet   (uint8_t bit_idx, bool do_set    = true);
    uint16_t addSenseCheck(uint8_t sense_idx, float threshold);
    // Pattern 1: MotivationCheck / SetMotivation
    uint16_t addMotivationCheck(MotivationType mot);
    uint16_t addSetMotivation  (MotivationType mot);
    // Pattern 3: Reference — delegates to another tree (resolved at build time).
    // name_hash: FNV-1a of the tree name, used by BTLoader::ResolveRefs() to
    // patch deferred nullptr references after all templates are loaded.
    uint16_t addReference(BehaviorTree* other, uint32_t name_hash = 0);
    // Patch all Reference nodes whose data == name_hash to point at target.
    void PatchReference(uint32_t name_hash, BehaviorTree* target) noexcept;
    // Pattern 6: GaugeCheck / GaugeSet
    uint16_t addGaugeCheck(GaugeType gauge, float threshold);
    uint16_t addGaugeSet  (GaugeType gauge, float value);

    // ── Extended AI node types ────────────────────────────────────────────────
    // C11: SequenceStateless — children always re-evaluated from index 0 on entry
    uint16_t addSequenceStateless();
    // C12: TimerStart with OnlyIncrease flag — does not shorten an existing timer
    uint16_t addTimerStartOnlyIncrease(uint8_t timer_id, uint32_t duration_ms);
    // C13: FrameFlag — single-tick signal cleared at start of each logic tick
    uint16_t addFrameFlagCheck(uint8_t bit_idx, bool check_set = true);
    uint16_t addFrameFlagSet  (uint8_t bit_idx, bool do_set    = true);
    // C14: WeightedSelector — up to 4 weighted children (weights need not sum to 100)
    uint16_t addWeightedSelector(const uint8_t weights[4]);
    // C15: AwarenessCheck
    uint16_t addAwarenessCheck(AwarenessState state);
    // C16: AlertnessCheck
    uint16_t addAlertnessCheck(AlertnessState state);
    // C17: MoodCheck
    uint16_t addMoodCheck(NpcMood mood);
    // C18: Role coordination via RoleRegistry singleton
    uint16_t addRoleCheck  (NpcRole role, bool check_could_perform = false);
    uint16_t addRoleClaim  (NpcRole role, uint32_t query_id);
    uint16_t addRoleRelease(NpcRole role);
    // C19: WithdrawState 3-stage retreat FSM
    uint16_t addWithdrawCheck(WithdrawState state);
    uint16_t addSetWithdraw  (WithdrawState state);
    // Echo NpcMemory nodes
    uint16_t addMemoryCheck (uint8_t mode);  // 0=has_spatial, 1=has_event
    uint16_t addMemoryForget();
    // MdDump: AreaSweepCheck
    uint16_t addAreaSweepCheck(AreaSweepType type);
    // MD_gemini patterns
    uint16_t addDecoratorPercentage(uint8_t pct);
    uint16_t addSelectorPercentage();
    uint16_t addSenseTimeCheck(uint8_t sense_idx, uint32_t max_elapsed_ms);
    uint16_t addActionSetDead();
    uint16_t addActionDespawn();
    // MD_deepseek
    uint16_t addAggroLevelCheck    (NpcAggroLevel level);
    uint16_t addSetAggroLevel      (NpcAggroLevel level);
    uint16_t addNpcCombatStateCheck(NpcCombatState state);
    uint16_t addSetNpcCombatState  (NpcCombatState state);
    // MD_z
    uint16_t addDecoratorMood      (NpcMood mood);
    uint16_t addDecoratorAwareness (AwarenessState state);
    // DecoratorTimerAuto: auto-start timer on every entry; propagate child result.
    // only_increase=true → never shorten an already-running timer (C12 semantics).
    uint16_t addDecoratorTimerAuto (uint8_t timer_id, uint32_t duration_ms,
                                    bool only_increase = false);
    // ActionTimerRandom: start timer with random duration in [min_ms, max_ms].
    // Both values are rounded to nearest 100ms (unit=100ms, 12-bit range, max=409.5s).
    uint16_t addActionTimerRandom  (uint8_t timer_id, uint32_t min_ms, uint32_t max_ms);
    uint16_t addActionSquadNotify  (SquadSignal signal);
    uint16_t addConditionSquadSignal(SquadSignal signal);
    // ConditionAnySenseWithinTime: Success if any sense fired within time_ms of nowMs.
    // With sense_idx set (specific=true), only that sense is checked.
    uint16_t addConditionAnySenseWithinTime(uint32_t time_ms, bool specific = false,
                                            uint8_t sense_idx = 0);
    uint16_t addActionExpireTimer  (uint8_t timer_id);
    // TargetFlagCheck: reads target entity from bb key fnv1a("target_entity"), checks lcflags.
    uint16_t addTargetFlagCheck    (uint8_t bit_idx, bool check_set = true);
    uint16_t addSetLocomotionState (LocomotionState state);
    // Pattern 4 (MD_arch): named branch gate from NamedBranchRegistry.
    // inverted=false → child runs when branch active; true → when inactive.
    uint16_t addDecoratorNamedBranch(const char* branch_name, bool inverted = false);
    uint16_t addDecoratorNamedBranch(uint32_t name_hash,      bool inverted = false);
    // MD_grok: 10 new patterns from BEHAVIOR XML analysis
    // ActionIdleTime: waits duration_ms then returns Success (uses internal st.timer).
    uint16_t addActionIdleTime(uint32_t duration_ms);
    // ConditionHaveTarget: Success if blackboard key "target_entity" holds a valid entity.
    uint16_t addConditionHaveTarget();
    // ConditionHaveNextTarget: same for "next_target_entity".
    uint16_t addConditionHaveNextTarget();
    // ActionTriggerSound: fires NpcSoundEvent via NpcSoundBus; always Success.
    uint16_t addActionTriggerSound(NpcSoundEvent ev);
    // ConditionIsSenseActivationAbove: check sc->activation[sense_idx] vs qualifier.
    uint16_t addConditionIsSenseActivationAbove(uint8_t sense_idx, SenseThresholdQualifier q);
    // ConditionHasAnySenseBeenAbove: Success if any sense was ever triggered at qualifier level.
    uint16_t addConditionHasAnySenseBeenAbove(SenseThresholdQualifier q);
    // ActionSuspiciousItemDoneStage: set investigation_stage on latest NpcEventRecord.
    uint16_t addActionSuspiciousItemDoneStage(SuspiciousItemStage stage);
    // ConditionSuspiciousItemBTPriority: Success if any event intensity >= min_intensity.
    uint16_t addConditionSuspiciousItemBTPriority(uint8_t min_intensity);
    // ConditionLastTimeSquadNotified: Success if squad channel received signal within time_s.
    uint16_t addConditionLastTimeSquadNotified(SquadSignal signal, uint8_t time_threshold_s);
    // DecoratorAggressionEscalation: gate child on as->aggro_level != None.
    uint16_t addDecoratorAggressionEscalation();

    // ── Batch 2 factories ─────────────────────────────────────────────────────
    uint16_t addConditionIsDead();
    // char_type: 0=Owner, 1=Target(from bb), 2=Both must be in vent
    uint16_t addConditionIsInVent(uint8_t char_type = 0);
    uint16_t addConditionCanBreakout();
    uint16_t addConditionIsBackstage();
    uint16_t addConditionIsPartOfNPCGroup();
    uint16_t addConditionAnotherAlienIsAttacking();
    uint16_t addConditionHasSearchedPos();
    uint16_t addConditionHasDoneSuspectMoveTo();
    uint16_t addActionSwitchToNextTarget();
    uint16_t addActionDoneSystematicSearch();

    // ── Batch 4: EventOrder ───────────────────────────────────────────────────
    uint16_t addConditionEventAOccuredAfterB(EventType a, EventType b);

    // ── Batch 5: Squad extensions ─────────────────────────────────────────────
    uint16_t addConditionSquadDoingEscalation();
    uint16_t addConditionSquadDoingSuspiciousWarning();
    uint16_t addDecoratorSquadSearch();

    // ── Batch 6: SuspiciousItem Group system ──────────────────────────────────
    uint16_t addConditionSuspiciousItemShouldDoStage(SuspiciousItemStage stage);
    uint16_t addConditionSuspiciousItemIsWithinDistance(float max_dist_m);
    uint16_t addConditionSuspiciousItemFirstGroupMember();
    uint16_t addConditionSuspiciousItemGroupAllowedToProgress();
    uint16_t addConditionSuspiciousItemGroupMembersRoutingTo();
    uint16_t addConditionSuspiciousItemWaitForGroupRouting();

    // ── Batch 7: SuspiciousItemReaction / AmbushType / NoiseType ─────────────
    uint16_t addSIReactionCheck (SuspiciousItemReaction reaction);
    uint16_t addSetSIReaction   (SuspiciousItemReaction reaction);
    uint16_t addAmbushTypeCheck (AmbushType type);
    uint16_t addSetAmbushType   (AmbushType type);
    uint16_t addNoiseTypeCheck  (NoiseType type);
    uint16_t addSetNoiseType    (NoiseType type);

    // ── Batch 8: SI lifecycle ─────────────────────────────────────────────────
    uint16_t addConditionSuspiciousItemValid();
    uint16_t addActionConsumeSuspiciousItem();
    // ActionForceMoveToSI: sets ff::COULD_DO_SUSPECT_TARGET_RESPONSE_MOVETO so
    // game-side nav reads nm->events[0].x/z and repaths to that position.
    uint16_t addActionForceMoveToSI();

    // ── Batch 9 (Step 5): Inter-NPC relationship nodes ────────────────────────
    // threshold: 0-255; mode: 0=>=threshold (friendly check), 1=<threshold (hostile check)
    // Target entity read from AgentBlackboard key fnv1a("target_entity").
    uint16_t addRelationshipTrustCheck(uint8_t threshold, uint8_t mode = 0);
    uint16_t addRelationshipFearCheck (uint8_t threshold, uint8_t mode = 0);

    // ── Batch 10: MD_z.md BEHAVIOR XML patterns ───────────────────────────────
    // ConditionIsAnySenseActivationAbove: Success if any sc->activation[i] >= qualifier
    uint16_t addConditionIsAnySenseActivationAbove(SenseThresholdQualifier q);
    // ActionMoveThroughTarget: sets ff::SHOULD_MOVE_THROUGH_TARGET + writes target_speed
    uint16_t addActionMoveThroughTarget(LocomotionTargetSpeed speed = LocomotionTargetSpeed::Fast);
    // ActionAdjustMenace: calls DirectorSystem::AdjustMenace(delta, mode); mode: 0=add,1=set,2=sub
    uint16_t addActionAdjustMenace(float delta, uint8_t mode = 0);

    // ── Batch 11: BEHAVIOR XML patterns (P2+P8+P10) ───────────────────────────
    // ConditionAngleToTarget: dot(NPC_forward, dir_to_target) within angle_degrees
    //   Uses WorldTransform::rot_y (sin/cos) + SenseComponent::last_known_x/z
    uint16_t addConditionAngleToTarget(float angle_degrees);
    // ConditionShouldSuspend: Success if lcf::IS_SUSPENDED is set on self
    uint16_t addConditionShouldSuspend();
    // ActionSuspendSelf: sets lcf::IS_SUSPENDED; BTSystem will skip this entity next tick
    uint16_t addActionSuspendSelf();
    // ConditionTargetDistLOS: Euclidean dist ≤ max_dist_m AND sc->activation[VISUAL] ≥ threshold_lo
    uint16_t addConditionTargetDistLOS(float max_dist_m);

    // ── Batch 12 factories ────────────────────────────────────────────────────
    // ConditionTargetRoutingDistance: path-cache nav distance ≤ max_dist_m (Euclidean fallback).
    uint16_t addConditionTargetRoutingDistance(float max_dist_m);
    // ConditionAlienIsAllowed: Success if DirectorSystem active preset permits this AlienActionType.
    uint16_t addConditionAlienIsAllowed(AlienActionType action);
    // DecoratorLockVent: acquire exclusive vent slot on entry; release on child return.
    uint16_t addDecoratorLockVent(uint8_t vent_id);

    // ── Batch 16 ──────────────────────────────────────────────────────────────
    // SequenceIgnoreChildFail: Sequence that skips Failure children, returns Success.
    uint16_t addSequenceIgnoreChildFail();

    // ── Batch 15 ──────────────────────────────────────────────────────────────
    // ConditionIsEnemyOfTarget: AllianceMatrix::IsEnemy(self.alliance_group, target.alliance_group)
    uint16_t addConditionIsEnemyOfTarget();
    // ActionForceIdle: always returns Running (skip this tick).
    uint16_t addActionForceIdle();
    // ConditionHasValidRouteToTarget: PathCache::Get from self to target position.
    uint16_t addConditionHasValidRouteToTarget();

    // ── Batch 17 ──────────────────────────────────────────────────────────────
    // ConditionIsCharacterClass: Success if as->character_class == cls
    uint16_t addConditionIsCharacterClass(CharacterClass cls);
    // ConditionIsInCover: Success if lcf::IS_IN_COVER set
    uint16_t addConditionIsInCover();
    // ConditionShouldUseCover: Success if as->aggro_level >= NpcAggroLevel::Warning
    uint16_t addConditionShouldUseCover();
    // ActionMoveToCover: sets ff::SHOULD_MOVE_TO_COVER; always Success
    uint16_t addActionMoveToCover();
    // ActionIdleInCover: sets lcf::IS_IN_COVER + combat_state=EnteredCover; always Running
    uint16_t addActionIdleInCover();

    // ── Batch 18 ──────────────────────────────────────────────────────────────
    // BehaviourMoodSetCheck: Success if as->behaviour_mood_set == mood_set
    uint16_t addBehaviourMoodSetCheck(BehaviourMoodSet mood_set);
    // SetBehaviourMoodSet: writes as->behaviour_mood_set; always Success
    uint16_t addSetBehaviourMoodSet(BehaviourMoodSet mood_set);
    // ViewconeTypeCheck: Success if as->viewcone_type == vtype
    uint16_t addViewconeTypeCheck(ViewconeType vtype);
    // SetViewconeType: writes as->viewcone_type; always Success
    uint16_t addSetViewconeType(ViewconeType vtype);
    // SensoryTypeCheck: Success if as->last_sensory_type == stype
    uint16_t addSensoryTypeCheck(SensoryType stype);
    // SetSensoryType: writes as->last_sensory_type; always Success
    uint16_t addSetSensoryType(SensoryType stype);

    // ── Batch 19 ──────────────────────────────────────────────────────────────
    // Weapon system — reads WeaponComponent on entity.
    uint16_t addConditionCurrentWeaponIsEquipped();
    uint16_t addConditionCurrentWeaponNeedsReloading();
    uint16_t addConditionHasMeleeAttackAvailable();
    // ActionWeaponEquip: sets WeaponComponent::is_equipped = true; always Success
    uint16_t addActionWeaponEquip();
    // ActionRangedShoot: sets ff::SHOULD_RANGED_SHOOT in frame_flags; always Success
    uint16_t addActionRangedShoot();
    // ConditionHasObjective: Success if bb["has_objective"] == true
    uint16_t addConditionHasObjective();
    // ConditionBehaviourMoodSetAbove: Success if as->behaviour_mood_set > threshold
    uint16_t addConditionBehaviourMoodSetAbove(BehaviourMoodSet threshold);

    // ── Batch 20 ──────────────────────────────────────────────────────────────
    uint16_t addActionSuccess();
    uint16_t addActionFail();
    uint16_t addDecoratorTimer(uint8_t timer_id, uint32_t duration_ms);
    uint16_t addActionIdle();
    uint16_t addConditionTargetIsWithinDistance(float max_dist_m);
    uint16_t addConditionHasAWeapon();
    uint16_t addConditionShouldProcessSuspiciousItem();

    // ── Batch 21 ──────────────────────────────────────────────────────────────
    uint16_t addActionMoveToTarget(LocomotionTargetSpeed speed = LocomotionTargetSpeed::Fast);
    uint16_t addActionMakeAggressive();
    uint16_t addConditionAllowedToAttackTarget();
    uint16_t addActionDead();
    uint16_t addActionMeleeAttack(uint8_t attack_type = 0);
    uint16_t addConditionIsCurrentCoverValid();

    // ── Batch 22 ──────────────────────────────────────────────────────────────
    uint16_t addConditionLastTimeSensed(uint8_t sense_idx, uint32_t max_elapsed_ms);
    uint16_t addActionPerformRole(NpcRole role);
    uint16_t addConditionWasSenseThresholdLastIncreaseActivationAbove(uint8_t sense_idx,
                                                                       SenseThresholdQualifier q);
    uint16_t addConditionTargetsWeaponHasAmmo();
    uint16_t addConditionTargetsWeaponHasProperty(uint8_t weapon_type);
    uint16_t addConditionHasSearchedMostRecentSensedPosition();

    // ── Batch 23 ──────────────────────────────────────────────────────────────
    uint16_t addActionIdleTimeFacingTarget(uint32_t duration_ms);

    uint16_t addActionIdleTimeFacingTargetMostRecentSensedPosition(uint32_t duration_ms);
    uint16_t addActionIdleTimeFacingSuspiciousItem(uint32_t duration_ms);
    uint16_t addActionRangedAim();
    uint16_t addActionSuspiciousItemReaction(SuspiciousItemReaction reaction);
    uint16_t addActionSuspectTargetResponse();

    // ── Batch 24 ──────────────────────────────────────────────────────────────
    uint16_t addActionRequestCover();
    uint16_t addConditionHasValidCoverToChangeTo();

    // ── Batch 25 ──────────────────────────────────────────────────────────────
    uint16_t addConditionHasScript();
    uint16_t addActionScript(uint32_t script_name_hash);

    // ── Batch 26 ──────────────────────────────────────────────────────────────
    uint16_t addConditionLastTimeSearchedWithinTime(uint32_t max_elapsed_ms);

    uint16_t addConditionAllowedToSearch();
    uint16_t addConditionHasDoneSuspectResponseMoveTo();
    uint16_t addConditionHasDoneSuspectResponseWithinTime(uint32_t max_elapsed_ms);
    uint16_t addConditionHasKilltrap();
    uint16_t addConditionHasMeleeAttackAvailableOrIsAttacking();
    uint16_t addConditionIsBranchActive(uint32_t branch_name_hash);
    uint16_t addConditionIsRequestingCover();
    uint16_t addConditionAllowedToPursueTarget();

    // ── Batch 27 ──────────────────────────────────────────────────────────────
    uint16_t addActionBreakout();
    uint16_t addActionMoveToMostRecentSensedPosition();
    uint16_t addActionMoveToNearestStandingPointToTarget();
    uint16_t addActionMoveToAttackTarget();
    uint16_t addActionChangeCover();

    // ── Batch 28 ──────────────────────────────────────────────────────────────
    uint16_t addConditionIsInTargetsWeaponRange(float max_dist_m);
    uint16_t addConditionIsCoverExposed();
    uint16_t addConditionHasLostTarget(uint32_t max_elapsed_ms);
    uint16_t addActionUpdateLastKnownPosition();
    uint16_t addActionRequestInvestigate();
    uint16_t addActionMarkTargetLost();
    uint16_t addActionForceRetreat();
    uint16_t addActionHoldPosition();

    // ── Batch 29 ──────────────────────────────────────────────────────────────
    uint16_t addConditionNpcDevelopmentStageAbove(NpcDevelopmentStage stage);
    uint16_t addConditionNpcHasAbility(uint16_t ability_mask);
    uint16_t addConditionIsHostileToPlayer();
    uint16_t addActionPerformAmbush();
    uint16_t addActionStartSearch();
    uint16_t addActionCallForHelp();
    uint16_t addActionTauntTarget();
    uint16_t addActionSurrenderSelf();

    // ── Batch 30 ──────────────────────────────────────────────────────────────
    uint16_t addConditionEventCountAbove(uint8_t min_count);
    uint16_t addConditionSpatialMemoryCountAbove(uint8_t min_count);
    uint16_t addConditionMotivationTicksAbove(uint8_t min_ticks);
    uint16_t addConditionHasVisualHistory();
    uint16_t addActionPursueTarget();
    uint16_t addActionCircleTarget();
    uint16_t addActionBackOff();
    uint16_t addActionCrouchMove();
    uint16_t addActionVault();

    // ── Batch 31 ──────────────────────────────────────────────────────────────
    uint16_t addConditionHasVentCloseToAlien(float radius_m);
    uint16_t addConditionHasFlankedVentCloseToPlayer(float radius_m);
    uint16_t addConditionTargetIsOnlyAccessibleCrouching();
    uint16_t addConditionAngleNPCToTargetsAimLessThan(float angle_deg);

    // ── Batch 32: TokenRegistry ───────────────────────────────────────────────
    uint16_t addConditionHasToken(uint32_t token_id);
    uint16_t addActionReleaseToken(uint32_t token_id);

    // ── Batch 33: RE HIGH-priority combat nodes ───────────────────────────────
    uint16_t addActionAbortMeleeAttack();
    uint16_t addActionGetOutOfTheWay();
    uint16_t addActionHitTargetAndRun();
    // direction: 0=toward 1=away 2=strafe_left 3=strafe_right
    uint16_t addActionMoveInDirection(uint8_t direction = 0);
    uint16_t addActionSuspend();
    uint16_t addActionTakeStep();
    uint16_t addActionThreatAware();
    uint16_t addActionThreatEscalation();
    uint16_t addConditionCanShootNow();
    // threshold_pct: 0-100; Success if hp_pct <= threshold (damaged state check)
    uint16_t addConditionCheckHealthState(uint8_t threshold_pct);
    uint16_t addConditionHasGroupAwarenessState(AwarenessState state);
    uint16_t addConditionHasMeleeBlockAvailable();
    uint16_t addConditionHasMeleeCounterAttackAvailable();
    // max_elapsed_ms: window in ms; Success if shot at within this window
    uint16_t addConditionLastTimeTargetShotAtMe(uint32_t max_elapsed_ms);
    uint16_t addConditionTargetIsInWeaponRange();
    uint16_t addConditionTargetIsTargetingMe();
    uint16_t addConditionTargetIsUsingMeleeAttack();
    // count: 0=infinite; repeats child until count successes or child Failure
    uint16_t addDecoratorLoop(uint32_t count = 0);

    // ── Batch 34: RE MEDIUM-priority nodes ───────────────────────────────────
    uint16_t addActionForceSearch();
    uint16_t addActionResetSearchJobs();
    uint16_t addActionSetFrameFlag(uint8_t bit_idx);
    uint16_t addActionSetGaugeAmount(GaugeType gauge, float value);
    uint16_t addConditionHasSenseActivationBeenAbove(uint8_t sense_idx, SenseThresholdQualifier q);
    uint16_t addConditionIsCoverTooClose(float min_dist_m);
    uint16_t addConditionIsInCombatArea();
    uint16_t addConditionMostRecentSenseActivationHasBeenAbove(SenseThresholdQualifier q);
    uint16_t addConditionNeedsToGetOutOfTheWay();
    uint16_t addConditionObjectiveIsInCombatArea();
    uint16_t addConditionObjectiveIsWithinDistance(float max_dist_m);
    uint16_t addConditionShouldFollow();
    uint16_t addConditionTargetIsInCombatArea();
    uint16_t addConditionTargetIsWithinAggroRadius(float radius_m);
    uint16_t addConditionTargetNearestStandPointIsWithinDistance(float max_dist_m);
    uint16_t addDecoratorSetSenseSet(uint8_t sense_set_id);
    uint16_t addDecoratorSuspiciousItemInProgress();
    uint16_t addSelectorLinear();
    uint16_t addSequenceLinear();

    // ── Batch 35 ──────────────────────────────────────────────────────────────
    uint16_t addActionApplyPrimaryDamageControlResponse();
    uint16_t addActionBrokenCover();
    uint16_t addActionMoveToObjective();
    uint16_t addActionSetLogicCharacterFlags(uint8_t bit_idx, bool do_set);
    uint16_t addConditionAllowedToDoSuspiciousWarning();
    uint16_t addConditionCanTakeStep();
    uint16_t addConditionGameIsDifficulty();
    uint16_t addConditionHasAnySenseBeenAboveWithinTime(SenseThresholdQualifier q, uint32_t time_ms);
    uint16_t addConditionIsCorpseTrap();
    uint16_t addConditionIsGaugeAmountBelow(GaugeType gauge, float threshold);
    uint16_t addConditionLastTimeWasAbleToShootTarget(uint32_t max_elapsed_ms);
    uint16_t addConditionRequiresPrimaryDamageControlResponse(uint8_t threshold_pct = 25);
    uint16_t addConditionTargetIsPlayer();
    uint16_t addConditionTargetIsWithinDistanceUnobscured(float max_dist_m);
    uint16_t addConditionTargetLogicCharacterFlags(uint8_t bit_idx, bool check_set = true);

    void addChild(uint16_t parent, uint16_t child);
    void setRoot (uint16_t node);

    bool isValid() const { return m_root != INVALID && m_nodeCount > 0; }

    BTStatus tick(md::EngineContext& ctx, entt::entity e, uint32_t nowMs);
    void     reset();

private:
    BTNode   m_nodes   [MAX_NODES];
    uint16_t m_children[MAX_CHILDREN];
    BTState  m_state   [MAX_NODES];

    uint16_t m_nodeCount  = 0;
    uint16_t m_childCount = 0;
    uint16_t m_root       = INVALID;
};
