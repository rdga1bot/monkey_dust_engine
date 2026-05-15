#pragma once
#include <monkey_dust/ecs/engine_context.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/ai/role_registry.h>
#include <monkey_dust/ai/squad_signal.h>
#include <monkey_dust/ai/named_branch.h>
#include <monkey_dust/ai/npc_sound.h>
#include <monkey_dust/components/npc_memory.h>
#include <monkey_dust/ai/suspicious_item_group.h>
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
    uint16_t addConditionIsInVent();
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
