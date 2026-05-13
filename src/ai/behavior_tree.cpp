#include <monkey_dust/ai/behavior_tree.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/sense_component.h>

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

uint16_t BehaviorTree::addBranch(BTConditionFunc cond) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::Branch);
    m_nodes[i].condition = cond;
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
    m_nodes[i].data = timer_id & 0x7u;
    return i;
}
uint16_t BehaviorTree::addFlagCheck(uint32_t mask, bool check_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::FlagCheck);
    m_nodes[i].data  = mask;
    m_nodes[i].flags = check_set ? 0u : 1u;
    return i;
}
uint16_t BehaviorTree::addFlagSet(uint32_t mask, bool do_set) {
    uint16_t i = m_nodeCount++;
    initNode(m_nodes[i], BTNodeType::FlagSet);
    m_nodes[i].data  = mask;
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

// Stackless VM tick
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
                if (nd.childCount == 0 || !nd.condition(ctx, e)) {
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
                if (tid < MAX_AGENT_TIMERS)
                    as->timers[tid] = static_cast<uint64_t>(nowMs) + dur_ms;
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        case BTNodeType::TimerCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t  tid      = static_cast<uint8_t>(nd.data & 0x7u);
            uint64_t deadline = as->timers[tid];
            if (deadline != 0u && static_cast<uint64_t>(nowMs) >= deadline) {
                as->timers[tid] = 0u;
                result = BTStatus::Success;
            } else {
                result = BTStatus::Failure;
            }
            pc = nd.parent; continue;
        }

        case BTNodeType::FlagCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            bool bits_set = (as->flags & nd.data) != 0u;
            result = (nd.flags == 0u ? bits_set : !bits_set)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::FlagSet: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                if (nd.flags == 0u) as->flags |=  nd.data;
                else                as->flags &= ~nd.data;
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

        default:
            goto exit_loop;
        }
    }

exit_loop:
    return result;
}
