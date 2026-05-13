#pragma once
#include <monkey_dust/ai/fnv.h>
#include <cstdint>

// ── AgentBlackboard entry ────────────────────────────────────────────────────
// CATHODE EntityInterface analog: typed parameter slot keyed by FNV-1a hash.
// type: 0=bool  1=int  2=float  3=vec3  4=enum
struct BlackboardEntry {
    uint32_t key;   // md::fnv1a("field_name") — compile-time or runtime
    uint8_t  type;
    uint8_t  _pad[3];
    union {
        bool     b;
        int32_t  i;
        float    f;
        float    v[3];
        uint32_t e;
    } val;
};
static_assert(sizeof(BlackboardEntry) == 20, "BlackboardEntry must be 20 bytes");

// ── AgentState ───────────────────────────────────────────────────────────────
// M18 component. Pairs with BTComponent on every AI entity.
// CATHODE analogs: AgentTimers[], AgentFlags, EntityInterface parameter bus.
//
// timers: absolute deadline in game-milliseconds; 0 = inactive.
//   timer_id 0..7 — mapped by BT TimerStart/TimerCheck nodes (M21).
// flags:   per-agent bitmask; checked by BT FlagCheck/FlagSet (M21).
// bb:      CATHODE-style blackboard; MAX_BB_ENTRIES=24 (RAM budget).
//   bb_count tracks live entries; search is linear (24 entries = trivial).
static constexpr int MAX_BB_ENTRIES = 24;
static constexpr int MAX_AGENT_TIMERS = 8;

struct AgentState {
    uint64_t        timers[MAX_AGENT_TIMERS];  // ms deadlines; 0 = inactive
    uint32_t        flags;
    int             bb_count;
    BlackboardEntry bb[MAX_BB_ENTRIES];
};

// ── Blackboard helpers ───────────────────────────────────────────────────────

inline BlackboardEntry* bb_find(AgentState& s, uint32_t key) noexcept {
    for (int i = 0; i < s.bb_count; ++i)
        if (s.bb[i].key == key) return &s.bb[i];
    return nullptr;
}

inline BlackboardEntry* bb_insert(AgentState& s, uint32_t key, uint8_t type) noexcept {
    BlackboardEntry* e = bb_find(s, key);
    if (e) return e;
    if (s.bb_count >= MAX_BB_ENTRIES) return nullptr;
    e = &s.bb[s.bb_count++];
    e->key  = key;
    e->type = type;
    return e;
}

inline void bb_set_float(AgentState& s, uint32_t key, float val) noexcept {
    if (BlackboardEntry* e = bb_insert(s, key, 2)) { e->val.f = val; }
}

inline void bb_set_bool(AgentState& s, uint32_t key, bool val) noexcept {
    if (BlackboardEntry* e = bb_insert(s, key, 0)) { e->val.b = val; }
}

inline void bb_set_int(AgentState& s, uint32_t key, int32_t val) noexcept {
    if (BlackboardEntry* e = bb_insert(s, key, 1)) { e->val.i = val; }
}

inline float bb_get_float(const AgentState& s, uint32_t key, float def = 0.f) noexcept {
    for (int i = 0; i < s.bb_count; ++i)
        if (s.bb[i].key == key) return s.bb[i].val.f;
    return def;
}

inline bool bb_get_bool(const AgentState& s, uint32_t key, bool def = false) noexcept {
    for (int i = 0; i < s.bb_count; ++i)
        if (s.bb[i].key == key) return s.bb[i].val.b;
    return def;
}
