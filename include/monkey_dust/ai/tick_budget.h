#pragma once
#include <cstdint>

// TickBudget — CATHODE TriggerProcessingContext::begin_cycle pattern:
// Tracks per-frame trigger budget; rejects low-priority triggers when over budget.
// begin_frame() resets counters; try_consume(priority) returns true only if
// budget allows. Higher priority (lower value) always granted first.
// Zero-overhead to query; no allocations.
//
// Maps to CATHODE's per-cycle trigger usage reporting (report_trigger_usage).

static constexpr uint8_t TICK_PRIORITY_HIGH   = 0;
static constexpr uint8_t TICK_PRIORITY_NORMAL = 128;
static constexpr uint8_t TICK_PRIORITY_LOW    = 255;

class TickBudget {
public:
    static TickBudget& Get() noexcept {
        static TickBudget inst;
        return inst;
    }

    // Call once at start of each logic tick.
    void begin_frame(uint32_t frame_idx) noexcept {
        frame_   = frame_idx;
        used_    = 0;
        dropped_ = 0;
    }

    // Request a budget slot. Returns true if granted.
    // priority: lower = more important (0 = always granted).
    bool try_consume(uint8_t priority = TICK_PRIORITY_NORMAL) noexcept {
        if (used_ >= capacity_) {
            // Only grant if priority is above (lower than) the cutoff.
            if (priority > priority_cutoff_) { ++dropped_; return false; }
        }
        ++used_;
        return true;
    }

    void        set_capacity(uint16_t cap)    noexcept { capacity_       = cap; }
    void        set_cutoff(uint8_t cutoff)    noexcept { priority_cutoff_ = cutoff; }

    uint16_t    used()     const noexcept { return used_; }
    uint16_t    dropped()  const noexcept { return dropped_; }
    uint16_t    capacity() const noexcept { return capacity_; }
    uint32_t    frame()    const noexcept { return frame_; }
    bool        over_budget() const noexcept { return used_ >= capacity_; }

    void reset_stats() noexcept { used_ = 0; dropped_ = 0; }

private:
    uint32_t frame_           = 0;
    uint16_t used_            = 0;
    uint16_t dropped_         = 0;
    uint16_t capacity_        = 64;   // default: 64 triggers per frame
    uint8_t  priority_cutoff_ = TICK_PRIORITY_HIGH;  // override threshold
    uint8_t  _pad             = 0;
};
