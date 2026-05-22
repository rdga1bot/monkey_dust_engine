#pragma once
// WorldEvents — faction raids, caravans, and world-level occurrences.
// Kenshi RE: world events are triggered by WorldSimulation tick based on
// faction prosperity/aggression thresholds.
//
// Events are pushed to a ring buffer; game code polls and spawns actual NPCs.
// Engine side only tracks the event metadata — no ECS entities here.

#include <cstdint>
#include <cstring>

enum class WorldEventType : uint8_t {
    None     = 0,
    Raid     = 1,   // hostile faction sends attackers toward a zone
    Caravan  = 2,   // trading faction sends merchants between towns
    Patrol   = 3,   // faction deploys a patrol squad in their territory
    Bounty   = 4,   // faction places a bounty on a specific NPC
    Migration= 5,   // population moves between zones (low-prosperity push)
    COUNT    = 6
};

struct WorldEvent {
    WorldEventType type          = WorldEventType::None;
    uint8_t        from_faction  = 0;
    uint8_t        to_faction    = 0;   // target faction (raids/bounties); 0=neutral
    uint8_t        strength      = 1;   // 1-255: raid size / caravan value / patrol size
    float          origin_x      = 0.f;
    float          origin_z      = 0.f;
    float          dest_x        = 0.f;
    float          dest_z        = 0.f;
};
static_assert(sizeof(WorldEvent) == 20, "WorldEvent must be 20 bytes");

class WorldEventBus {
public:
    static constexpr int MAX_EVENTS = 64;

    static WorldEventBus& Get() noexcept {
        static WorldEventBus inst;
        return inst;
    }

    // Push a new event; drops silently if ring buffer is full.
    void Push(const WorldEvent& ev) noexcept {
        int next = (head_ + 1) % MAX_EVENTS;
        if (next == tail_) return;  // full
        events_[head_] = ev;
        head_ = next;
        ++total_pushed_;
    }

    // Pop the next pending event; returns false if empty.
    bool Pop(WorldEvent& out) noexcept {
        if (tail_ == head_) return false;
        out  = events_[tail_];
        tail_= (tail_ + 1) % MAX_EVENTS;
        return true;
    }

    bool IsEmpty() const noexcept { return tail_ == head_; }
    int  Pending() const noexcept {
        int n = head_ - tail_;
        return n < 0 ? n + MAX_EVENTS : n;
    }

    void Reset() noexcept { head_ = tail_ = 0; total_pushed_ = 0; }

    uint32_t TotalPushed() const noexcept { return total_pushed_; }

private:
    WorldEventBus() { memset(events_, 0, sizeof(events_)); }
    WorldEvent events_[MAX_EVENTS] = {};
    int        head_         = 0;
    int        tail_         = 0;
    uint32_t   total_pushed_ = 0;
};
