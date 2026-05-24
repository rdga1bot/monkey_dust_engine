#pragma once
#include <cstdint>
#include <monkey_dust/components/agent_state.h>

// ── NpcSchedule ───────────────────────────────────────────────────────────────
// Per-NPC daily schedule: up to 8 time-slots that override motivation.
// Applied by ScheduleSystem::Update() at 1 Hz (every 10 logic ticks).
// Only assigned to named/important NPCs; anonymous NPCs use default wander/patrol.

struct ScheduleSlot {
    uint8_t  start_hour;  // 0..23 inclusive
    uint8_t  end_hour;    // 0..23 inclusive (wraps across midnight: end < start ok)
    uint8_t  motivation;  // MotivationType enum value to push
    uint8_t  priority;    // 0=low, 255=high; overrides BT only if > as.sched_priority
};
static_assert(sizeof(ScheduleSlot) == 4, "ScheduleSlot must be 4 bytes");

struct NpcSchedule {
    static constexpr int MAX_SLOTS = 8;
    ScheduleSlot slots[MAX_SLOTS];
    uint8_t      slot_count = 0;
    uint8_t      _pad[3]   = {};

    // Returns slot index [0, slot_count) active at game_hour, or -1 if none.
    int FindActiveSlot(float game_hour) const {
        uint8_t h = (uint8_t)(int)game_hour;
        int best = -1;
        uint8_t best_prio = 0;
        for (int i = 0; i < slot_count; ++i) {
            const ScheduleSlot& s = slots[i];
            bool active;
            if (s.start_hour <= s.end_hour)
                active = (h >= s.start_hour && h < s.end_hour);
            else  // wraps midnight (e.g. 22→6)
                active = (h >= s.start_hour || h < s.end_hour);
            if (active && s.priority >= best_prio) {
                best = i; best_prio = s.priority;
            }
        }
        return best;
    }

    // Helpers to build common schedules without runtime allocation.
    void AddSlot(uint8_t start, uint8_t end, MotivationType mot, uint8_t prio = 100) {
        if (slot_count >= MAX_SLOTS) return;
        slots[slot_count++] = { start, end, (uint8_t)mot, prio };
    }

    static NpcSchedule MakeGuard() {
        NpcSchedule s;
        s.AddSlot(22, 6,  MotivationType::Rest,    200);
        s.AddSlot(6,  8,  MotivationType::Idle,    180);
        s.AddSlot(8,  20, MotivationType::Idle,    100);  // Idle drives wander/patrol BT
        s.AddSlot(20, 22, MotivationType::Idle,    50);
        return s;
    }

    static NpcSchedule MakeVendor() {
        NpcSchedule s;
        s.AddSlot(22, 7,  MotivationType::Rest,    200);
        s.AddSlot(7,  9,  MotivationType::Idle,    150);
        s.AddSlot(9,  21, MotivationType::Trade,   100);
        s.AddSlot(21, 22, MotivationType::Idle,    50);
        return s;
    }

    static NpcSchedule MakeWorker() {
        NpcSchedule s;
        s.AddSlot(23, 6,  MotivationType::Rest,    200);
        s.AddSlot(6,  7,  MotivationType::Idle,    150);
        s.AddSlot(7,  19, MotivationType::Work,    100);
        s.AddSlot(19, 23, MotivationType::Idle,    50);
        return s;
    }
};
static_assert(sizeof(NpcSchedule) == 36, "NpcSchedule size check");
