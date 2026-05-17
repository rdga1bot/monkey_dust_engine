#include <monkey_dust/scripting/event_scheduler.h>
#include <monkey_dust/scripting/flow_graph.h>

void MdEventScheduler::Tick(uint32_t now_ms, double now_s, FlowGraph& fg) noexcept {
    int i = 0;
    while (i < count_) {
        MdScheduledEvent& e = events_[i];
        if (!e.active) { ++i; continue; }

        bool should_fire = false;

        switch (e.type) {
            case MdEventType::OneShot:
                // Fire immediately on first Tick (fire_at_ms starts as NOT_READY).
                if (e.fire_at_ms == MD_SCHED_NOT_READY)
                    e.fire_at_ms = now_ms;  // fire now
                should_fire = (now_ms >= e.fire_at_ms);
                break;

            case MdEventType::Delayed:
                // Set fire_at_ms on first tick (now_ms + delay_ms).
                if (e.fire_at_ms == MD_SCHED_NOT_READY)
                    e.fire_at_ms = now_ms + e.delay_ms;
                should_fire = (now_ms >= e.fire_at_ms);
                break;

            case MdEventType::Cooldown:
                // Only fires when RequestFire() sets fire_at_ms.
                should_fire = (e.fire_at_ms != MD_SCHED_NOT_READY &&
                               now_ms >= e.fire_at_ms);
                break;

            case MdEventType::FireOnLoad:
            case MdEventType::FireOnClear:
                // Fires only after OnLoad/OnClear sets fire_at_ms.
                should_fire = (e.fire_at_ms != MD_SCHED_NOT_READY &&
                               now_ms >= e.fire_at_ms);
                break;

            case MdEventType::Repeating:
                if (e.fire_at_ms == MD_SCHED_NOT_READY)
                    e.fire_at_ms = now_ms;  // first fire: now
                should_fire = (now_ms >= e.fire_at_ms);
                break;
        }

        if (should_fire) {
            fg.FireTrigger(e.node_id, now_s);
            e.last_fire_ms = now_ms;

            switch (e.type) {
                case MdEventType::OneShot:
                case MdEventType::Delayed:
                case MdEventType::FireOnLoad:
                case MdEventType::FireOnClear:
                    // Discard — swap-remove.
                    events_[i] = events_[--count_];
                    continue;
                case MdEventType::Cooldown:
                    e.fire_at_ms = MD_SCHED_NOT_READY;  // reset; wait for next RequestFire
                    break;
                case MdEventType::Repeating:
                    e.fire_at_ms = now_ms + e.cooldown_ms;  // schedule next
                    break;
            }
        }
        ++i;
    }
}
