#include <monkey_dust/ai/behavior_tree.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/bt_components.h>
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/components/npc_memory.h>
#include <monkey_dust/ai/squad_signal.h>
#include <monkey_dust/ai/fnv.h>
#include <cstring>

// Compile-time key for target entity stored in AgentBlackboard
static constexpr uint32_t TARGET_ENTITY_BB_KEY = md::fnv1a("target_entity");

BehaviorTree::BehaviorTree() {
    memset(m_nodes,    0, sizeof(m_nodes));
    memset(m_children, 0, sizeof(m_children));
    memset(m_state,    0, sizeof(m_state));
}

static inline void initNode(BTNode& n, BTNodeType t) {
    n.type       = t;
    n.flags      = 0;
    n.parent     = BehaviorTree::INVALID;
    n.childStart = 0;
    n.childCount = 0;
    n.data       = 0;
    n._padding   = nullptr;
}

// ── M21 factory implementations ───────────────────────────────────────────────

uint16_t BehaviorTree::addBranch(BTConditionFunc cond, BranchType btype, ShutdownSpeed speed) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::Branch);
    m_nodes[i].condition = cond;
    m_nodes[i].data      = static_cast<uint32_t>(btype);
    m_nodes[i].flags     = static_cast<uint8_t>(speed);
    return i;
}
uint16_t BehaviorTree::addTimerStart(uint8_t timer_id, uint32_t duration_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::TimerStart);
    m_nodes[i].data = (static_cast<uint32_t>(timer_id) << 24) | (duration_ms & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addTimerCheck(uint8_t timer_id) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::TimerCheck);
    m_nodes[i].data = timer_id & 0x1Fu;  // 5 bits: 0-31 (covers all 26 slots)
    return i;
}
// Pattern 4: bit_idx = lcf::* constant (0-63)
uint16_t BehaviorTree::addFlagCheck(uint8_t bit_idx, bool check_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::FlagCheck);
    m_nodes[i].data  = bit_idx & 0x3Fu;  // 6 bits: 0-63
    m_nodes[i].flags = check_set ? 0u : 1u;
    return i;
}
uint16_t BehaviorTree::addFlagSet(uint8_t bit_idx, bool do_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::FlagSet);
    m_nodes[i].data  = bit_idx & 0x3Fu;
    m_nodes[i].flags = do_set ? 0u : 1u;
    return i;
}
uint16_t BehaviorTree::addSenseCheck(uint8_t sense_idx, float threshold) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SenseCheck);
    uint32_t t_fixed = static_cast<uint32_t>(threshold * 1000.f + 0.5f);
    m_nodes[i].data  = (static_cast<uint32_t>(sense_idx & 1u) << 24) | (t_fixed & 0x00FFFFFFu);
    return i;
}

// ── Pattern 1: MotivationCheck / SetMotivation ────────────────────────────────
uint16_t BehaviorTree::addMotivationCheck(MotivationType mot) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::MotivationCheck);
    m_nodes[i].data = static_cast<uint32_t>(mot);
    return i;
}
uint16_t BehaviorTree::addSetMotivation(MotivationType mot) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SetMotivation);
    m_nodes[i].data = static_cast<uint32_t>(mot);
    return i;
}

// ── Pattern 3: Reference ──────────────────────────────────────────────────────
uint16_t BehaviorTree::addReference(BehaviorTree* other, uint32_t name_hash) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::Reference);
    m_nodes[i]._padding = other;
    m_nodes[i].data     = name_hash;
    return i;
}

void BehaviorTree::PatchReference(uint32_t name_hash, BehaviorTree* target) noexcept {
    for (uint16_t i = 0; i < m_nodeCount; ++i) {
        if (m_nodes[i].type == BTNodeType::Reference && m_nodes[i].data == name_hash)
            m_nodes[i]._padding = target;
    }
}

// ── Pattern 6: GaugeCheck / GaugeSet ─────────────────────────────────────────
uint16_t BehaviorTree::addGaugeCheck(GaugeType gauge, float threshold) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::GaugeCheck);
    uint32_t t_fixed = static_cast<uint32_t>(threshold * 1000.f + 0.5f);
    m_nodes[i].data  = (static_cast<uint32_t>(gauge) << 24) | (t_fixed & 0x00FFFFFFu);
    return i;
}
uint16_t BehaviorTree::addGaugeSet(GaugeType gauge, float value) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::GaugeSet);
    uint32_t v_fixed = static_cast<uint32_t>(value * 1000.f + 0.5f);
    m_nodes[i].data  = (static_cast<uint32_t>(gauge) << 24) | (v_fixed & 0x00FFFFFFu);
    return i;
}

// ── Extended AI node types ───────────────────────────────────────────────────

// C11
uint16_t BehaviorTree::addSequenceStateless() {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SequenceStateless);
    return i;
}

// C12: same as addTimerStart but sets flag bit 0 (only_increase)
uint16_t BehaviorTree::addTimerStartOnlyIncrease(uint8_t timer_id, uint32_t duration_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::TimerStart);
    m_nodes[i].data  = (static_cast<uint32_t>(timer_id) << 24) | (duration_ms & 0x00FFFFFFu);
    m_nodes[i].flags = 0x01u; // only_increase
    return i;
}

// C13
uint16_t BehaviorTree::addFrameFlagCheck(uint8_t bit_idx, bool check_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::FrameFlagCheck);
    m_nodes[i].data  = bit_idx & 0x3Fu;
    m_nodes[i].flags = check_set ? 0u : 1u;
    return i;
}
uint16_t BehaviorTree::addFrameFlagSet(uint8_t bit_idx, bool do_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::FrameFlagSet);
    m_nodes[i].data  = bit_idx & 0x3Fu;
    m_nodes[i].flags = do_set ? 0u : 1u;
    return i;
}

// C14: weights[0..3] packed as uint8 into data
uint16_t BehaviorTree::addWeightedSelector(const uint8_t weights[4]) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::WeightedSelector);
    m_nodes[i].data = static_cast<uint32_t>(weights[0])
                    | (static_cast<uint32_t>(weights[1]) <<  8)
                    | (static_cast<uint32_t>(weights[2]) << 16)
                    | (static_cast<uint32_t>(weights[3]) << 24);
    return i;
}

// C15
uint16_t BehaviorTree::addAwarenessCheck(AwarenessState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::AwarenessCheck);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}

// C16
uint16_t BehaviorTree::addAlertnessCheck(AlertnessState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::AlertnessCheck);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}

// C17
uint16_t BehaviorTree::addMoodCheck(NpcMood mood) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::MoodCheck);
    m_nodes[i].data = static_cast<uint32_t>(mood);
    return i;
}

// C18
uint16_t BehaviorTree::addRoleCheck(NpcRole role, bool check_could_perform) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::RoleCheck);
    m_nodes[i].data = (static_cast<uint32_t>(role) << 8) | (check_could_perform ? 1u : 0u);
    return i;
}
uint16_t BehaviorTree::addRoleClaim(NpcRole role, uint32_t query_id) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::RoleClaim);
    m_nodes[i].data = (query_id << 8) | (static_cast<uint32_t>(role) & 0xFFu);
    return i;
}
uint16_t BehaviorTree::addRoleRelease(NpcRole role) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::RoleRelease);
    m_nodes[i].data = static_cast<uint32_t>(role);
    return i;
}

// C19
uint16_t BehaviorTree::addWithdrawCheck(WithdrawState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::WithdrawCheck);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}
uint16_t BehaviorTree::addSetWithdraw(WithdrawState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SetWithdraw);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}

uint16_t BehaviorTree::addMemoryCheck(uint8_t mode) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::MemoryCheck);
    m_nodes[i].data = mode;
    return i;
}

uint16_t BehaviorTree::addMemoryForget() {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::MemoryForget);
    return i;
}

uint16_t BehaviorTree::addAreaSweepCheck(AreaSweepType type) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::AreaSweepCheck);
    m_nodes[i].data = static_cast<uint32_t>(type);
    return i;
}

uint16_t BehaviorTree::addDecoratorPercentage(uint8_t pct) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::DecoratorPercentage);
    m_nodes[i].data = pct;
    return i;
}

uint16_t BehaviorTree::addSelectorPercentage() {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SelectorPercentage);
    return i;
}

uint16_t BehaviorTree::addSenseTimeCheck(uint8_t sense_idx, uint32_t max_elapsed_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SenseTimeCheck);
    m_nodes[i].data = (static_cast<uint32_t>(sense_idx) << 24) | (max_elapsed_ms & 0x00FFFFFFu);
    return i;
}

uint16_t BehaviorTree::addActionSetDead() {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionSetDead);
    return i;
}

uint16_t BehaviorTree::addActionDespawn() {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionDespawn);
    return i;
}

// CATHODE_deepseek: AggroLevelCheck / SetAggroLevel / NpcCombatStateCheck / SetNpcCombatState
uint16_t BehaviorTree::addAggroLevelCheck(NpcAggroLevel level) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::AggroLevelCheck);
    m_nodes[i].data = static_cast<uint32_t>(level);
    return i;
}
uint16_t BehaviorTree::addSetAggroLevel(NpcAggroLevel level) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SetAggroLevel);
    m_nodes[i].data = static_cast<uint32_t>(level);
    return i;
}
uint16_t BehaviorTree::addNpcCombatStateCheck(NpcCombatState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::NpcCombatStateCheck);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}
uint16_t BehaviorTree::addSetNpcCombatState(NpcCombatState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SetNpcCombatState);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}

// ── CATHODE_z factory implementations ────────────────────────────────────────

uint16_t BehaviorTree::addDecoratorMood(NpcMood mood) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::DecoratorMood);
    m_nodes[i].data = static_cast<uint32_t>(mood);
    return i;
}

uint16_t BehaviorTree::addDecoratorAwareness(AwarenessState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::DecoratorAwareness);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}

uint16_t BehaviorTree::addDecoratorTimerAuto(uint8_t timer_id, uint32_t duration_ms,
                                              bool only_increase) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::DecoratorTimerAuto);
    m_nodes[i].data  = (static_cast<uint32_t>(timer_id) << 24) | (duration_ms & 0x00FFFFFFu);
    m_nodes[i].flags = only_increase ? 0x01u : 0x00u;
    return i;
}

uint16_t BehaviorTree::addActionTimerRandom(uint8_t timer_id, uint32_t min_ms, uint32_t max_ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionTimerRandom);
    // Round to nearest 100ms unit (12 bits each, max 4095*100=409,500ms)
    uint32_t min_u = (min_ms / 100u) & 0xFFFu;
    uint32_t max_u = (max_ms / 100u) & 0xFFFu;
    if (max_u < min_u) max_u = min_u;
    m_nodes[i].data = (static_cast<uint32_t>(timer_id) << 24) | (max_u << 12) | min_u;
    return i;
}

uint16_t BehaviorTree::addActionSquadNotify(SquadSignal signal) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionSquadNotify);
    m_nodes[i].data = static_cast<uint32_t>(signal);
    return i;
}

uint16_t BehaviorTree::addConditionSquadSignal(SquadSignal signal) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionSquadSignal);
    m_nodes[i].data = static_cast<uint32_t>(signal);
    return i;
}

uint16_t BehaviorTree::addConditionAnySenseWithinTime(uint32_t time_ms, bool specific,
                                                       uint8_t sense_idx) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ConditionAnySenseWithinTime);
    // data bits 0-27 = time_window_ms; bit 28 = sense_idx (0 or 1)
    m_nodes[i].data  = (time_ms & 0x0FFFFFFFu) | (static_cast<uint32_t>(sense_idx & 1u) << 28);
    m_nodes[i].flags = specific ? 1u : 0u;
    return i;
}

uint16_t BehaviorTree::addActionExpireTimer(uint8_t timer_id) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::ActionExpireTimer);
    m_nodes[i].data = timer_id & 0x1Fu;
    return i;
}

uint16_t BehaviorTree::addTargetFlagCheck(uint8_t bit_idx, bool check_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::TargetFlagCheck);
    m_nodes[i].data  = bit_idx & 0x3Fu;
    m_nodes[i].flags = check_set ? 0u : 1u;
    return i;
}

uint16_t BehaviorTree::addSetLocomotionState(LocomotionState state) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::SetLocomotionState);
    m_nodes[i].data = static_cast<uint32_t>(state);
    return i;
}

// ── Existing factories ────────────────────────────────────────────────────────

uint16_t BehaviorTree::addSelector()  { uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::Selector);  return i; }
uint16_t BehaviorTree::addSequence()  { uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::Sequence);  return i; }
uint16_t BehaviorTree::addInverter()  { uint16_t i = m_nodeCount++; initNode(m_nodes[i], BTNodeType::Inverter);  return i; }

uint16_t BehaviorTree::addRepeat(uint32_t count) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::Repeat);
    m_nodes[i].data = count;
    return i;
}
uint16_t BehaviorTree::addWait(uint32_t ms) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::Wait);
    m_nodes[i].data = ms;
    return i;
}
uint16_t BehaviorTree::addCondition(BTConditionFunc func) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::Condition);
    m_nodes[i].condition = func;
    return i;
}
uint16_t BehaviorTree::addAction(BTActionFunc func) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::Action);
    m_nodes[i].action = func;
    return i;
}

void BehaviorTree::addChild(uint16_t parent, uint16_t child) {
    BTNode& pn = m_nodes[parent];
    if (pn.childCount == 0)
        pn.childStart = m_childCount;
    m_children[m_childCount++] = child;
    pn.childCount++;
    m_nodes[child].parent = parent;
}

void BehaviorTree::setRoot(uint16_t node) { m_root = node; }

void BehaviorTree::reset() {
    memset(m_state, 0, sizeof(BTState) * m_nodeCount);
}

// ── Stackless VM tick ─────────────────────────────────────────────────────────
BTStatus BehaviorTree::tick(md::EngineContext& ctx, entt::entity e, uint32_t nowMs) {
    if (!isValid()) return BTStatus::Failure;

    uint16_t pc     = m_root;
    BTStatus result = BTStatus::Running;

    static constexpr int VM_MAX_STEPS = 4096;
    int vm_steps = 0;

    while (true) {
        if (++vm_steps > VM_MAX_STEPS) break;
        if (pc == INVALID) break;

        BTNode&  nd = m_nodes[pc];
        BTState& st = m_state[pc];

        switch (nd.type) {

        case BTNodeType::Selector:
            if (result == BTStatus::Success) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            if (result == BTStatus::Failure) {
                st.currentChild++;
            } else {
                st.currentChild = 0;
            }
            if (st.currentChild >= nd.childCount) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            pc = m_children[nd.childStart + st.currentChild];
            result = BTStatus::Running; continue;

        case BTNodeType::Sequence:
            if (result == BTStatus::Failure) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            if (result == BTStatus::Success) {
                st.currentChild++;
            }
            if (st.currentChild >= nd.childCount) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            pc = m_children[nd.childStart + st.currentChild];
            result = BTStatus::Running; continue;

        case BTNodeType::Condition:
            result = nd.condition(ctx, e) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;

        case BTNodeType::Action:
            result = nd.action(ctx, e);
            if (result == BTStatus::Running) goto exit_loop;
            pc = nd.parent; continue;

        case BTNodeType::Inverter:
            if (result == BTStatus::Running) {
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            result = (result == BTStatus::Success) ? BTStatus::Failure : BTStatus::Success;
            pc = nd.parent; continue;

        case BTNodeType::Repeat:
            if (result == BTStatus::Running) {
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            if (result == BTStatus::Failure) {
                st.counter = 0;
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            st.counter++;
            if (nd.data > 0 && st.counter >= nd.data) {
                st.counter = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            if (nd.childCount > 0) {
                uint16_t ci = m_children[nd.childStart];
                m_state[ci] = BTState{};
                pc = ci; result = BTStatus::Running; continue;
            }
            pc = nd.parent; result = BTStatus::Success; continue;

        case BTNodeType::Wait:
            if (st.timer == 0)
                st.timer = nowMs + nd.data;
            if (static_cast<int32_t>(nowMs - st.timer) >= 0) {
                st.timer = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            result = BTStatus::Running;
            goto exit_loop;

        // ── M21 ──────────────────────────────────────────────────────────────

        case BTNodeType::Branch:
            if (result == BTStatus::Running) {
                // Re-evaluated every tick: persistent interrupt gate
                if (nd.childCount == 0 || (nd.condition && !nd.condition(ctx, e))) {
                    pc = nd.parent; result = BTStatus::Failure; continue;
                }
                pc = m_children[nd.childStart];
                result = BTStatus::Running; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is

        case BTNodeType::TimerStart: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t  tid    = static_cast<uint8_t>(nd.data >> 24);
                uint32_t dur_ms = nd.data & 0x00FFFFFFu;
                if (tid < MAX_AGENT_TIMERS) {
                    uint64_t new_end = static_cast<uint64_t>(nowMs) + dur_ms;
                    // C12: only_increase — don't shorten an already-running timer
                    if ((nd.flags & 0x01u) && as->timers[tid] > new_end) {
                        // existing deadline is later; leave it untouched
                    } else {
                        as->timers[tid] = new_end;
                    }
                }
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        case BTNodeType::TimerCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t  tid      = static_cast<uint8_t>(nd.data & 0x1Fu);
            if (tid >= MAX_AGENT_TIMERS) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint64_t deadline = as->timers[tid];
            if (deadline != 0u && static_cast<uint64_t>(nowMs) >= deadline) {
                as->timers[tid] = 0u;
                result = BTStatus::Success;
            } else {
                result = BTStatus::Failure;
            }
            pc = nd.parent; continue;
        }

        // Pattern 4: bit-index encoding (lcf::* constants, 0-63)
        case BTNodeType::FlagCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
            bool    is_set  = as->lcflags.test(bit_idx);
            result = (nd.flags == 0u ? is_set : !is_set)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::FlagSet: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
                if (nd.flags == 0u) as->lcflags.set  (bit_idx);
                else                as->lcflags.clear(bit_idx);
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        case BTNodeType::SenseCheck: {
            SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t sense_idx = static_cast<uint8_t>(nd.data >> 24);
            float   threshold = static_cast<float>(nd.data & 0x00FFFFFFu) * 0.001f;
            if (sense_idx > 1u) sense_idx = 0u;
            result = (sc->activation[sense_idx] >= threshold)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // ── AI behavior patterns adaptations ───────────────────────────────────────

        // Pattern 1: check current motivation matches expected
        case BTNodeType::MotivationCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->motivation == static_cast<MotivationType>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Pattern 1: write motivation to agent state
        case BTNodeType::SetMotivation: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->motivation = static_cast<MotivationType>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Pattern 3: delegate to referenced tree
        case BTNodeType::Reference: {
            BehaviorTree* other = static_cast<BehaviorTree*>(nd._padding);
            if (!other) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = other->tick(ctx, e, nowMs);
            if (result == BTStatus::Running) goto exit_loop;
            pc = nd.parent; continue;
        }

        // Pattern 6: gauge threshold check
        case BTNodeType::GaugeCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            auto    gtype = static_cast<GaugeType>(nd.data >> 24);
            float   thr   = static_cast<float>(nd.data & 0x00FFFFFFu) * 0.001f;
            result = (as->gauges.get(gtype) >= thr)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Pattern 6: gauge write (set to fixed value)
        case BTNodeType::GaugeSet: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                auto  gtype = static_cast<GaugeType>(nd.data >> 24);
                float val   = static_cast<float>(nd.data & 0x00FFFFFFu) * 0.001f;
                as->gauges.set(gtype, val);
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // ── AI-11–20 ─────────────────────────────────────────────────────

        // C11: SequenceStateless — always restarts from child 0 on re-entry
        case BTNodeType::SequenceStateless:
            if (result == BTStatus::Failure) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            if (result == BTStatus::Success) {
                st.currentChild++;
            } else { // Running — stateless: reset to 0 every entry
                st.currentChild = 0;
            }
            if (st.currentChild >= nd.childCount) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            pc = m_children[nd.childStart + st.currentChild];
            result = BTStatus::Running; continue;

        // C13: FrameFlag — read/write frame_flags (cleared each logic tick)
        case BTNodeType::FrameFlagCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
            bool    is_set  = (as->frame_flags >> bit_idx) & 1ull;
            result = (nd.flags == 0u ? is_set : !is_set)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::FrameFlagSet: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
                if (nd.flags == 0u) as->frame_flags |=  (1ull << bit_idx);
                else                as->frame_flags &= ~(1ull << bit_idx);
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // C14: WeightedSelector — one-shot weighted child pick per entry
        case BTNodeType::WeightedSelector:
            if (result == BTStatus::Success || result == BTStatus::Failure) {
                st.currentChild = 0;
                pc = nd.parent; continue; // propagate child result as-is
            }
            // First entry (Running): pick weighted random child
            {
                if (nd.childCount == 0) {
                    pc = nd.parent; result = BTStatus::Failure; continue;
                }
                uint8_t w[4] = {
                    static_cast<uint8_t>( nd.data        & 0xFFu),
                    static_cast<uint8_t>((nd.data >>  8) & 0xFFu),
                    static_cast<uint8_t>((nd.data >> 16) & 0xFFu),
                    static_cast<uint8_t>((nd.data >> 24) & 0xFFu),
                };
                // LCG step seeded by entity id XOR frame_index
                uint32_t rng = static_cast<uint32_t>(static_cast<uint64_t>(e))
                               ^ ctx.frame_index;
                rng = rng * 1664525u + 1013904223u;
                uint8_t  nc    = nd.childCount < 4u ? static_cast<uint8_t>(nd.childCount) : 4u;
                uint32_t total = 0;
                for (uint8_t ii = 0; ii < nc; ++ii) total += w[ii];
                if (total == 0u) total = 1u;
                uint32_t roll    = rng % total;
                uint8_t  chosen  = nc - 1u;
                uint32_t acc     = 0u;
                for (uint8_t ii = 0; ii < nc; ++ii) {
                    acc += w[ii];
                    if (roll < acc) { chosen = ii; break; }
                }
                st.currentChild = chosen;
                pc = m_children[nd.childStart + (chosen % nd.childCount)];
                result = BTStatus::Running; continue;
            }

        // C15: AwarenessCheck
        case BTNodeType::AwarenessCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->awareness == static_cast<AwarenessState>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // C16: AlertnessCheck
        case BTNodeType::AlertnessCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->alertness == static_cast<AlertnessState>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // C17: MoodCheck
        case BTNodeType::MoodCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->mood == static_cast<NpcMood>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // C18: Role coordination
        case BTNodeType::RoleCheck: {
            auto  role    = static_cast<NpcRole>(nd.data >> 8);
            bool  do_could = (nd.data & 0xFFu) != 0u;
            bool  ok = do_could ? RoleRegistry::Get().could_perform(role)
                                : RoleRegistry::Get().is_performing(role, e);
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::RoleClaim: {
            auto     role     = static_cast<NpcRole>(nd.data & 0xFFu);
            uint32_t query_id = nd.data >> 8;
            // Prefer dynamic query_id from DirectorHintComponent when the hint
            // targets this same role — scopes the claim to the Director's session.
            if (const DirectorHintComponent* hint =
                    Registry::Get().try_get<DirectorHintComponent>(e)) {
                if (hint->role_pending && hint->suggested_role == role)
                    query_id = hint->query_id;
            }
            result = RoleRegistry::Get().claim(role, query_id, e)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::RoleRelease: {
            auto role = static_cast<NpcRole>(nd.data);
            RoleRegistry::Get().release(role, e);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // C19: WithdrawState
        case BTNodeType::WithdrawCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->withdraw_state == static_cast<WithdrawState>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::SetWithdraw: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->withdraw_state = static_cast<WithdrawState>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Echo NpcMemory nodes
        case BTNodeType::MemoryCheck: {
            NpcMemoryComponent* mem = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (!mem) { result = BTStatus::Failure; pc = nd.parent; continue; }
            bool ok = (nd.data == 0) ? (mem->spatial_count > 0) : (mem->event_count > 0);
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::MemoryForget: {
            NpcMemoryComponent* mem = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (mem) mem->ClearAll();
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        case BTNodeType::AreaSweepCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (static_cast<uint8_t>(as->area_sweep_type) == static_cast<uint8_t>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // CATHODE_gemini: DecoratorPercentage — run child with data% probability
        case BTNodeType::DecoratorPercentage:
            if (result == BTStatus::Running) {
                // LCG seeded by entity XOR frame_index (same pattern as WeightedSelector)
                uint32_t rng = static_cast<uint32_t>(static_cast<uint64_t>(e)) ^ ctx.frame_index;
                rng = rng * 1664525u + 1013904223u;
                uint8_t pct = static_cast<uint8_t>(nd.data & 0xFFu);
                if ((rng % 100u) < pct && nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is

        // CATHODE_gemini: SelectorPercentage — pick one random child, return its result
        case BTNodeType::SelectorPercentage:
            if (result == BTStatus::Success || result == BTStatus::Failure) {
                pc = nd.parent; continue;  // propagate child result
            }
            {
                if (nd.childCount == 0) { result = BTStatus::Failure; pc = nd.parent; continue; }
                uint32_t rng = static_cast<uint32_t>(static_cast<uint64_t>(e)) ^ ctx.frame_index;
                rng = rng * 1664525u + 1013904223u;
                uint16_t chosen = static_cast<uint16_t>(rng % nd.childCount);
                pc = m_children[nd.childStart + chosen];
                result = BTStatus::Running; continue;
            }

        // CATHODE_gemini: SenseTimeCheck — Success if sense was triggered recently
        case BTNodeType::SenseTimeCheck: {
            auto* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t  sense_idx      = static_cast<uint8_t>((nd.data >> 24) & 0xFFu);
            uint32_t max_elapsed_ms = nd.data & 0x00FFFFFFu;
            if (sense_idx >= 2u) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint32_t elapsed = nowMs - sc->last_activated_ms[sense_idx];
            result = (elapsed <= max_elapsed_ms) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // CATHODE_gemini: ActionSetDead / ActionDespawn — deferred lifecycle via lcf flags
        case BTNodeType::ActionSetDead: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            as->lcflags.set(lcf::IS_DEAD);
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        case BTNodeType::ActionDespawn: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            as->lcflags.set(lcf::SHOULD_DESPAWN);
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        // CATHODE_deepseek: AggroLevelCheck / SetAggroLevel
        case BTNodeType::AggroLevelCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->aggro_level == static_cast<NpcAggroLevel>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::SetAggroLevel: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->aggro_level = static_cast<NpcAggroLevel>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // CATHODE_deepseek: NpcCombatStateCheck / SetNpcCombatState
        case BTNodeType::NpcCombatStateCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->combat_state == static_cast<NpcCombatState>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::SetNpcCombatState: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->combat_state = static_cast<NpcCombatState>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // ── CATHODE_z VM cases ─────────────────────────────────────────────────

        // Z1: DecoratorMood — run child only when as->mood matches
        case BTNodeType::DecoratorMood: {
            if (result == BTStatus::Running) {
                AgentState* as = Registry::Get().try_get<AgentState>(e);
                if (!as || as->mood != static_cast<NpcMood>(nd.data)) {
                    result = BTStatus::Failure; pc = nd.parent; continue;
                }
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is
        }

        // Z2: DecoratorAwareness — run child only when as->awareness matches
        case BTNodeType::DecoratorAwareness: {
            if (result == BTStatus::Running) {
                AgentState* as = Registry::Get().try_get<AgentState>(e);
                if (!as || as->awareness != static_cast<AwarenessState>(nd.data)) {
                    result = BTStatus::Failure; pc = nd.parent; continue;
                }
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is
        }

        // Z3: DecoratorTimerAuto — start timer on every entry, propagate child result
        case BTNodeType::DecoratorTimerAuto: {
            if (result == BTStatus::Running) {
                AgentState* as = Registry::Get().try_get<AgentState>(e);
                if (as) {
                    uint8_t  tid    = static_cast<uint8_t>(nd.data >> 24);
                    uint32_t dur_ms = nd.data & 0x00FFFFFFu;
                    if (tid < MAX_AGENT_TIMERS) {
                        uint64_t new_end = static_cast<uint64_t>(nowMs) + dur_ms;
                        if ((nd.flags & 0x01u) && as->timers[tid] > new_end) {
                            // only_increase: leave longer deadline intact
                        } else {
                            as->timers[tid] = new_end;
                        }
                    }
                }
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                result = BTStatus::Success; pc = nd.parent; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is
        }

        // Z4: ActionTimerRandom — start timer with LCG-random duration [min_ms, max_ms]
        case BTNodeType::ActionTimerRandom: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t  tid   = static_cast<uint8_t>(nd.data >> 24);
                uint32_t max_u = (nd.data >> 12) & 0xFFFu;   // 12 bits, unit=100ms
                uint32_t min_u =  nd.data        & 0xFFFu;
                if (tid < MAX_AGENT_TIMERS) {
                    uint32_t range = (max_u > min_u) ? (max_u - min_u) : 0u;
                    uint32_t rng   = static_cast<uint32_t>(static_cast<uint64_t>(e)) ^ nowMs;
                    rng = rng * 1664525u + 1013904223u;
                    uint32_t dur_u   = min_u + (range > 0u ? rng % range : 0u);
                    uint64_t new_end = static_cast<uint64_t>(nowMs)
                                     + static_cast<uint64_t>(dur_u) * 100u;
                    as->timers[tid] = new_end;
                }
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Z5: ActionSquadNotify — broadcast SquadSignal to entity's squad channel
        case BTNodeType::ActionSquadNotify: {
            SquadMemberComponent* sm = Registry::Get().try_get<SquadMemberComponent>(e);
            if (sm) {
                auto sig = static_cast<SquadSignal>(nd.data & 0xFFu);
                uint32_t raw_id = static_cast<uint32_t>(static_cast<uint64_t>(e) & 0xFFFFFFFFu);
                SquadSignalBus::Get().Set(sm->squad_id, sig, raw_id, nowMs);
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Z6: ConditionSquadSignal — check squad channel for expected signal
        case BTNodeType::ConditionSquadSignal: {
            SquadMemberComponent* sm = Registry::Get().try_get<SquadMemberComponent>(e);
            if (!sm) { result = BTStatus::Failure; pc = nd.parent; continue; }
            auto expected = static_cast<SquadSignal>(nd.data & 0xFFu);
            result = (SquadSignalBus::Get().GetSignal(sm->squad_id) == expected)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Z7: ConditionAnySenseWithinTime — any (or specific) sense fired within time_ms
        case BTNodeType::ConditionAnySenseWithinTime: {
            SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint32_t time_ms  = nd.data & 0x0FFFFFFFu;
            uint8_t  sense_idx= static_cast<uint8_t>((nd.data >> 28) & 0x1u);
            bool     specific = (nd.flags != 0u);
            bool     ok       = false;
            if (specific) {
                uint32_t ts = sc->last_activated_ms[sense_idx];
                ok = (ts != 0u && static_cast<uint32_t>(nowMs) - ts <= time_ms);
            } else {
                for (int ii = 0; ii < 2 && !ok; ++ii) {
                    uint32_t ts = sc->last_activated_ms[ii];
                    ok = (ts != 0u && static_cast<uint32_t>(nowMs) - ts <= time_ms);
                }
            }
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Z8: ActionExpireTimer — immediately mark timer slot as expired
        case BTNodeType::ActionExpireTimer: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t tid = static_cast<uint8_t>(nd.data & 0x1Fu);
                if (tid < MAX_AGENT_TIMERS)
                    as->timers[tid] = 1u;  // past-deadline: nowMs >= 1 always true
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Z9: TargetFlagCheck — check lcflags on target entity from blackboard
        case BTNodeType::TargetFlagCheck: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            const BlackboardEntry* en = bb_find(*ab, TARGET_ENTITY_BB_KEY);
            if (!en) { result = BTStatus::Failure; pc = nd.parent; continue; }
            entt::entity target = static_cast<entt::entity>(en->val.e);
            AgentState*  as_t   = Registry::Get().try_get<AgentState>(target);
            if (!as_t) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
            bool    is_set  = as_t->lcflags.test(bit_idx);
            result = (nd.flags == 0u ? is_set : !is_set)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Z10: SetLocomotionState — write as->locomotion_state
        case BTNodeType::SetLocomotionState: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->locomotion_state = static_cast<LocomotionState>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        default:
            goto exit_loop;
        }
    }

exit_loop:
    return result;
}
