#pragma once
#include <monkey_dust/ecs/engine_context.h>
#include <entt/entt.hpp>
#include <cstdint>
#include <cstring>

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
    Wait
};

// Leaf functions accept engine context; game side casts to GameState& (which inherits EngineContext)
using BTConditionFunc = bool    (*)(md::EngineContext&, entt::entity);
using BTActionFunc    = BTStatus(*)(md::EngineContext&, entt::entity);

// BTNode: 24 bytes, cache-line friendly, flat array — no heap
struct BTNode {
    BTNodeType type;
    uint8_t    flags;
    uint16_t   parent;
    uint16_t   childStart;
    uint16_t   childCount;
    uint32_t   data;        // Repeat: count; Wait: ms
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
