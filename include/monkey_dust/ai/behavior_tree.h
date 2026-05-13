#pragma once
#include <monkey_dust/ecs/engine_context.h>
#include <monkey_dust/components/agent_state.h>
#include <entt/entt.hpp>
#include <cstdint>

// ── Pattern 2: ShutdownSpeed ──────────────────────────────────────────────────
// CATHODE RequestShutDownSpeed analog. Stored in BTNode::flags for Branch nodes.
enum class ShutdownSpeed : uint8_t {
    Graceful = 0,  // SST_GRACEFULL — finish current action before stopping
    Normal   = 1,  // SST_NORMAL
    Critical = 2,  // SST_CRITICAL — abort immediately
};

// ── Pattern 2: BranchType ─────────────────────────────────────────────────────
// CATHODE BEHAVIOR_TREE_BRANCH_TYPE analog.
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
    // M21 — CATHODE LegendPlugin analogs
    Branch,         // persistent interrupt: re-checks nd.condition every tick before child
    TimerStart,     // leaf: as->timers[slot] = nowMs + duration_ms
    TimerCheck,     // leaf condition: expired? clears timer on fire
    FlagCheck,      // leaf condition: as->lcflags.test(bit_idx); nd.flags=1 → check clear
    FlagSet,        // leaf action: set(nd.flags=0) or clear(nd.flags=1) bit at nd.data
    SenseCheck,     // leaf condition: activation[idx] >= threshold
    // CATHODE adaptations (Patterns 1, 3, 6)
    MotivationCheck,// leaf condition: as->motivation == (MotivationType)data
    SetMotivation,  // leaf action: as->motivation = (MotivationType)data → Success
    Reference,      // delegates tick to another BehaviorTree* stored in _padding
    GaugeCheck,     // leaf condition: as->gauges.get(type) >= threshold
    GaugeSet,       // leaf action: as->gauges.set(type, value) → Success
};

// Leaf functions accept engine context; game side casts to GameState& (which inherits EngineContext)
using BTConditionFunc = bool    (*)(md::EngineContext&, entt::entity);
using BTActionFunc    = BTStatus(*)(md::EngineContext&, entt::entity);

// BTNode: 24 bytes, cache-line friendly, flat array — no heap
// data encoding per node type:
//   TimerStart:      (timer_id << 24) | duration_ms
//   TimerCheck:      timer_id & 0x1Fu  (AgentTimerSlot value)
//   FlagCheck/Set:   bit_idx (0-63, maps to lcf::* constants)
//   SenseCheck:      (sense_idx << 24) | (threshold * 1000)
//   MotivationCheck: MotivationType value (uint8_t)
//   SetMotivation:   MotivationType value (uint8_t)
//   Reference:       unused (BehaviorTree* in _padding)
//   GaugeCheck:      (GaugeType << 24) | (threshold * 1000)
//   GaugeSet:        (GaugeType << 24) | (value * 1000)
//   Branch:          BranchType in lower byte (semantic only)
// flags encoding per node type:
//   FlagCheck:       0=check set, 1=check clear
//   FlagSet:         0=set bit, 1=clear bit
//   Branch:          ShutdownSpeed (upper nibble, currently unused in VM)
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
    // Pattern 3: Reference — delegates to another tree (resolved at build time)
    uint16_t addReference(BehaviorTree* other);
    // Pattern 6: GaugeCheck / GaugeSet
    uint16_t addGaugeCheck(GaugeType gauge, float threshold);
    uint16_t addGaugeSet  (GaugeType gauge, float value);

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
