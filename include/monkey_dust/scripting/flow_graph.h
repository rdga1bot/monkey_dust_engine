#pragma once
#include <monkey_dust/ai/fnv.h>
#include <entt/entt.hpp>
#include <cstdint>

// ── FlowGraph ─────────────────────────────────────────────────────────────────
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

// M45: typed flow variable.
// SaveSystem only persists Float-typed vars (FlowVarRecord).
// Condition nodes coerce any type to float for evaluation.
enum class FlowVarType : uint8_t { Float = 0, Bool = 1, Int = 2, Str = 3 };

struct FlowVar {
    uint32_t    key;      // FNV-1a hash of variable name
    FlowVarType type;
    uint8_t     _pad[3];
    union {
        float   f;
        bool    b;
        int32_t i;
        char    s[16];    // inline string (max 15 chars + NUL)
    } val;
};
static_assert(sizeof(FlowVar) == 24, "FlowVar must be 24 bytes");;

// one pending event in the ring buffer.
// fire_at_s = absolute game time (matches TriggerInfo::duration semantics).
struct FlowPendingTrigger {
    uint32_t node_id;    // target node to fire
    uint32_t conn_idx;   // source connection index (INVALID_CONN = external fire)
    double   fire_at_s;
};

static constexpr uint32_t FLOW_INVALID_CONN  = 0xFFFFFFFFu;
static constexpr uint8_t  FLOW_INVALID_DURABLE = 0xFFu;

// ── C20: FlowDurableTrigger ───────────────────────────────────────────────────
// One-shot FlowPendingTriggers fire once and are discarded.
// Durable triggers persist for `duration` seconds and can be ref-counted by
// multiple consumers. Released when ref_count reaches 0 OR duration expires.
//
// Usage:
//   slot = graph.AcquireDurable(node_id, duration_s)   // ref_count=1
//   graph.AddRef(slot)                                  // ref_count++
//   graph.Release(slot)                                 // ref_count--; free when 0
//   FlowGraph::Tick automatically decrements duration and calls Release on expiry.
struct FlowDurableTrigger {
    uint32_t node_id;    // target node
    float    duration;   // remaining seconds; 0 = expired / free slot
    uint8_t  ref_count;
    uint8_t  _pad[3];
};
static_assert(sizeof(FlowDurableTrigger) == 12, "FlowDurableTrigger must be 12 bytes");

struct FlowGraph {
    static constexpr int MAX_NODES           = 64;
    static constexpr int MAX_CONNS           = 128;
    static constexpr int MAX_VARS            = 32;
    static constexpr int MAX_PENDING         = 32;   // ring buffer — power of 2
    static constexpr int MAX_PARAMS          = 128;  // float parameter pool
    static constexpr int MAX_ACTIONS         = 16;   // registered action callbacks
    static constexpr int MAX_DURABLE_TRIGGERS= 16;   // C20: durable trigger pool

    FlowNode             nodes  [MAX_NODES];
    FlowConn             conns  [MAX_CONNS];
    FlowVar              vars   [MAX_VARS];
    FlowPendingTrigger   pending[MAX_PENDING];
    float                params [MAX_PARAMS];
    FlowDurableTrigger   durable[MAX_DURABLE_TRIGGERS]; // C20 pool

    int node_count, conn_count, var_count, params_count;
    int pending_head, pending_tail;  // ring buffer indices

    struct ActionEntry { uint32_t node_id; FlowActionFunc func; };
    ActionEntry actions[MAX_ACTIONS];
    int         action_count;

    void Init();

    // C20: Durable trigger pool management.
    // AcquireDurable: allocate a slot (ref_count=1, duration=duration_s).
    //   Returns FLOW_INVALID_DURABLE if pool is full.
    uint8_t AcquireDurable(uint32_t node_id, float duration_s);
    void    AddRef (uint8_t slot);    // increment ref_count (no-op on invalid slot)
    void    Release(uint8_t slot);    // decrement ref_count; free slot when reaches 0

    // External trigger: fire node at fire_at_s.
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

    // Typed variable accessors (FNV-1a key).
    // GetVar / SetVar (float) — backward-compatible; coerce any type to float.
    float       GetVar    (uint32_t key, float def = 0.f) const;
    void        SetVar    (uint32_t key, float value);

    // Typed setters — create or update variable with exact type.
    void        SetVarBool(uint32_t key, bool  value);
    void        SetVarInt (uint32_t key, int32_t value);
    void        SetVarStr (uint32_t key, const char* value);  // max 15 chars

    // Typed getters — return typed value; falls back to def when missing/wrong type.
    bool        GetVarBool(uint32_t key, bool        def = false) const;
    int32_t     GetVarInt (uint32_t key, int32_t     def = 0)     const;
    const char* GetVarStr (uint32_t key, const char* def = "")    const;

    // Returns Float for unknown keys.
    FlowVarType GetVarType(uint32_t key) const;

private:
    bool  ring_push(const FlowPendingTrigger& t);
    bool  ring_pop (FlowPendingTrigger& t);
    bool  ring_peek(FlowPendingTrigger& t) const;

    FlowNode* find_node(uint32_t id);
    FlowActionFunc find_action(uint32_t node_id) const;
    void propagate(uint32_t from_node_id, uint8_t from_port, double now_s,
                   entt::entity ctx, entt::registry& reg);
};
