#pragma once
#include <cstdint>

class FlowGraph;  // forward declaration — include flow_graph.h before calling Tick()

// ── MdEventScheduler ──────────────────────────────────────────────────────────
// Flare EventComponent-inspired scheduling layer on top of FlowGraph events.
// Adds cooldown, delay, keep_after_trigger, and lifecycle hooks
// (OnLoad / OnClear) without modifying FlowGraph internals.
//
// Relationship to FlowGraph:
//   FlowGraph::FireTrigger(node_id, fire_at_s) — fires once at a time.
//   MdEventScheduler::Schedule() wraps that with refire/cooldown logic.
//
// Usage:
//   // At scene load:
//   auto& sched = MdEventScheduler::Get();
//   sched.Schedule(fnv1a("open_door"), MdEventType::Delayed, 0, 2000);
//   sched.Schedule(fnv1a("spawn_wave"), MdEventType::Repeating, 5000, 0);
//   sched.Schedule(fnv1a("play_intro"), MdEventType::FireOnLoad, 0, 500);
//   sched.OnLoad(now_ms);  // triggers FireOnLoad events
//
//   // Each logic tick:
//   sched.Tick(now_ms, now_s, ctx_entity, reg);
//
//   // When area is cleared:
//   sched.OnClear(now_ms);  // triggers FireOnClear events

enum class MdEventType : uint8_t {
    OneShot     = 0,  // fires once, then discarded (same as FlowGraph one-shot)
    Cooldown    = 1,  // fires on demand; blocked for cooldown_ms after each fire
    Delayed     = 2,  // fires once after delay_ms; then discarded
    Repeating   = 3,  // fires every cooldown_ms; keep_after_trigger=true
    FireOnLoad  = 4,  // fires once on next OnLoad() call (+ optional delay_ms)
    FireOnClear = 5,  // fires once on next OnClear() call (+ optional delay_ms)
};

static constexpr uint32_t MD_SCHED_NOT_READY = 0xFFFFFFFFu;  // fire_at_ms sentinel

struct MdScheduledEvent {
    uint32_t     node_id      = 0;
    MdEventType  type         = MdEventType::OneShot;
    bool         active       = false;
    uint8_t      _pad[2]      = {};
    uint32_t     cooldown_ms  = 0;              // min ms between firings (Cooldown/Repeating)
    uint32_t     delay_ms     = 0;              // ms before first fire
    uint32_t     last_fire_ms = 0;              // timestamp of last firing
    uint32_t     fire_at_ms   = MD_SCHED_NOT_READY;  // UINT32_MAX = not yet scheduled
};

class MdEventScheduler {
public:
    static constexpr int MAX_EVENTS = 32;

    static MdEventScheduler& Get() {
        static MdEventScheduler inst;
        return inst;
    }

    // Schedule a new event. Returns false if full or node_id==0.
    // Duplicate node_id + type → updates existing entry.
    bool Schedule(uint32_t node_id, MdEventType type,
                  uint32_t cooldown_ms = 0, uint32_t delay_ms = 0) noexcept {
        if (!node_id || count_ >= MAX_EVENTS) return false;
        for (int i = 0; i < count_; ++i) {
            if (events_[i].node_id == node_id && events_[i].type == type) {
                events_[i].cooldown_ms = cooldown_ms;
                events_[i].delay_ms    = delay_ms;
                events_[i].active      = true;
                return true;
            }
        }
        MdScheduledEvent& e = events_[count_++];
        e.node_id      = node_id;
        e.type         = type;
        e.cooldown_ms  = cooldown_ms;
        e.delay_ms     = delay_ms;
        e.active       = true;
        e.last_fire_ms = 0;
        e.fire_at_ms   = MD_SCHED_NOT_READY;  // 0 would cause immediate fire
        return true;
    }

    // Activate FireOnLoad events — call once at scene/area load.
    void OnLoad(uint32_t now_ms) noexcept {
        for (int i = 0; i < count_; ++i) {
            auto& e = events_[i];
            if (!e.active || e.type != MdEventType::FireOnLoad) continue;
            e.fire_at_ms = now_ms + e.delay_ms;  // 0+0=0 is valid "fire now"
        }
    }

    // Activate FireOnClear events — call when area is cleared (enemies defeated).
    void OnClear(uint32_t now_ms) noexcept {
        for (int i = 0; i < count_; ++i) {
            auto& e = events_[i];
            if (!e.active || e.type != MdEventType::FireOnClear) continue;
            e.fire_at_ms = now_ms + e.delay_ms;
        }
    }

    // Manually request firing of a Cooldown event. Returns false if on cooldown.
    bool RequestFire(uint32_t node_id, uint32_t now_ms) noexcept {
        for (int i = 0; i < count_; ++i) {
            auto& e = events_[i];
            if (e.node_id != node_id || !e.active) continue;
            if (e.type != MdEventType::Cooldown) continue;
            // Block if still within cooldown window of last fire.
            if (e.last_fire_ms > 0 && now_ms - e.last_fire_ms < e.cooldown_ms) return false;
            e.fire_at_ms = now_ms;
            return true;
        }
        return false;
    }

    // Process due events. Fires via fg.FireTrigger(node_id, now_s).
    // Include flow_graph.h before calling Tick().
    void Tick(uint32_t now_ms, double now_s, FlowGraph& fg) noexcept;

    void Cancel(uint32_t node_id) noexcept {
        for (int i = 0; i < count_; ++i)
            if (events_[i].node_id == node_id) events_[i].active = false;
    }

    void Clear() noexcept { count_ = 0; }
    int  Count() const noexcept { return count_; }
    int  ActiveCount() const noexcept {
        int n = 0;
        for (int i = 0; i < count_; ++i) n += events_[i].active ? 1 : 0;
        return n;
    }

private:
    MdEventScheduler() = default;
    MdScheduledEvent events_[MAX_EVENTS] = {};
    int              count_ = 0;
};
