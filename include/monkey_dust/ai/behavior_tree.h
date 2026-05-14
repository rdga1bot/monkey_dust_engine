#pragma once
#include <monkey_dust/ecs/engine_context.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/ai/role_registry.h>
#include <entt/entt.hpp>
#include <cstdint>

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
    // CathodeDump: AreaSweepCheck — gates on as->area_sweep_type == (AreaSweepType)data
    AreaSweepCheck,
    // CATHODE_gemini patterns:
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
    // CATHODE_deepseek patterns:
    //   AggroLevelCheck: data=(NpcAggroLevel)→Success if as->aggro_level matches
    //   SetAggroLevel:   data=(NpcAggroLevel)→writes and returns Success
    AggroLevelCheck,
    SetAggroLevel,
    //   NpcCombatStateCheck: data=(NpcCombatState)→Success if as->combat_state matches
    //   SetNpcCombatState:   data=(NpcCombatState)→writes and returns Success
    NpcCombatStateCheck,
    SetNpcCombatState,
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
// flags encoding per node type:
//   FlagCheck:        0=check set, 1=check clear
//   FlagSet:          0=set bit, 1=clear bit
//   FrameFlagCheck:   0=check set, 1=check clear
//   FrameFlagSet:     0=set bit, 1=clear bit
//   TimerStart:       bit 0 = only_increase (C12: don't shorten existing timer)
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
    // CathodeDump: AreaSweepCheck
    uint16_t addAreaSweepCheck(AreaSweepType type);
    // CATHODE_gemini patterns
    uint16_t addDecoratorPercentage(uint8_t pct);
    uint16_t addSelectorPercentage();
    uint16_t addSenseTimeCheck(uint8_t sense_idx, uint32_t max_elapsed_ms);
    uint16_t addActionSetDead();
    uint16_t addActionDespawn();
    // CATHODE_deepseek
    uint16_t addAggroLevelCheck    (NpcAggroLevel level);
    uint16_t addSetAggroLevel      (NpcAggroLevel level);
    uint16_t addNpcCombatStateCheck(NpcCombatState state);
    uint16_t addSetNpcCombatState  (NpcCombatState state);

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
