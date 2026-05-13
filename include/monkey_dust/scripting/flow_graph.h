#pragma once
#include <monkey_dust/ai/fnv.h>
#include <entt/entt.hpp>
#include <cstdint>

// ── FlowGraph ─────────────────────────────────────────────────────────────────
// CATHODE TriggerInfo + EntityInterface dispatch analog.
// Event-driven graph: nodes fire triggers that propagate through connections.
// All storage is fixed-size (BSS) — no malloc in Tick path.
//
// Node types:
//   0=Event    — fires outgoing connections immediately when triggered
//   1=Condition— checks FlowVar value; true port=0, false port=1
//   2=Action   — calls registered FlowActionFunc, then propagates
//   3=Delay    — re-schedules trigger with fire_at_s + params[param_offset]
//   4=Variable — sets FlowVar key=params[param_offset] val=params[param_offset+1]

// Callback invoked when an Action node fires.
using FlowActionFunc = void(*)(uint32_t node_id, double now_s, entt::entity ctx, entt::registry& reg);

struct FlowNode {
    uint32_t id;           // FNV-1a hash of node name
    uint8_t  type;         // 0=Event 1=Condition 2=Action 3=Delay 4=Variable
    uint8_t  conn_count;   // number of outgoing connections from this node
    uint16_t param_offset; // index into FlowGraph::params[]
};
static_assert(sizeof(FlowNode) == 8, "FlowNode must be 8 bytes");

struct FlowConn {
    uint32_t from_node, to_node;
    uint8_t  from_port, to_port;
    uint8_t  _pad[2];
};
static_assert(sizeof(FlowConn) == 12, "FlowConn must be 12 bytes");

struct FlowVar {
    uint32_t key;   // FNV-1a hash of variable name
    float    value;
};

// CATHODE TriggerInfo analog: one pending event in the ring buffer.
// fire_at_s = absolute game time (matches TriggerInfo::duration semantics).
struct FlowPendingTrigger {
    uint32_t node_id;    // target node to fire
    uint32_t conn_idx;   // source connection index (INVALID_CONN = external fire)
    double   fire_at_s;
};

static constexpr uint32_t FLOW_INVALID_CONN = 0xFFFFFFFFu;

struct FlowGraph {
    static constexpr int MAX_NODES   = 64;
    static constexpr int MAX_CONNS   = 128;
    static constexpr int MAX_VARS    = 32;
    static constexpr int MAX_PENDING = 32;  // ring buffer — power of 2
    static constexpr int MAX_PARAMS  = 128; // float parameter pool
    static constexpr int MAX_ACTIONS = 16;  // registered action callbacks

    FlowNode           nodes  [MAX_NODES];
    FlowConn           conns  [MAX_CONNS];
    FlowVar            vars   [MAX_VARS];
    FlowPendingTrigger pending[MAX_PENDING];
    float              params [MAX_PARAMS];

    int node_count, conn_count, var_count, params_count;
    int pending_head, pending_tail;  // ring buffer indices

    struct ActionEntry { uint32_t node_id; FlowActionFunc func; };
    ActionEntry actions[MAX_ACTIONS];
    int         action_count;

    void Init();

    // External trigger: fire node at fire_at_s (CATHODE queue_level analog).
    // fire_at_s == 0.0 → fire on next Tick.
    void FireTrigger(uint32_t node_id, double fire_at_s);

    // Register a callback for Action nodes. Must be called before LoadFromJson.
    void RegisterAction(uint32_t node_id, FlowActionFunc func);
    void RegisterAction(const char* node_name, FlowActionFunc func) {
        RegisterAction(md::fnv1a(node_name), func);
    }

    // Process all expired triggers. Propagates connections; dispatches actions.
    void Tick(double now_s, entt::entity context, entt::registry& reg);

    // Load graph structure from a *.flow.json file.
    bool LoadFromJson(const char* path);

    // Variable accessors (FNV-1a key).
    float GetVar(uint32_t key, float def = 0.f) const;
    void  SetVar(uint32_t key, float value);

private:
    bool  ring_push(const FlowPendingTrigger& t);
    bool  ring_pop (FlowPendingTrigger& t);
    bool  ring_peek(FlowPendingTrigger& t) const;

    FlowNode* find_node(uint32_t id);
    FlowActionFunc find_action(uint32_t node_id) const;
    void propagate(uint32_t from_node_id, uint8_t from_port, double now_s,
                   entt::entity ctx, entt::registry& reg);
};
