#include <monkey_dust/ai/behavior_tree.h>

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

        default:
            goto exit_loop;
        }
    }

exit_loop:
    return result;
}
