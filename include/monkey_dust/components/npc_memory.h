#pragma once
#include <cstdint>

// ── NpcMemoryComponent ────────────────────────────────────────────────────────
// Echo-inspired autonomous NPC memory: spatial observations + event history.
// Flat POD arrays — no heap. One component per NPC entity.
//
// Spatial memories: last-seen positions of targets/threats (8 slots, LRU evict).
// Event memories: FNV-1a IDs of events witnessed by this NPC (8 slots, ring).
//
// BT nodes that read this component:
//   MemoryCheck(HasSpatial) → Success if spatial_count > 0
//   MemoryCheck(HasEvent)   → Success if event_count > 0
//   MemoryForget            → clears spatial + event memory → Success

struct SpatialMemory {
    float    x            = 0.f;
    float    z            = 0.f;
    uint32_t timestamp_ms = 0;
    uint8_t  confidence   = 0;   // 0=none, 255=certain
    uint8_t  _pad[3]      = {};
};
static_assert(sizeof(SpatialMemory) == 16, "");

struct NpcMemoryComponent {
    static constexpr uint8_t MAX_SPATIAL = 8;
    static constexpr uint8_t MAX_EVENTS  = 8;

    SpatialMemory spatial[MAX_SPATIAL]  = {};
    uint32_t      events [MAX_EVENTS]   = {};
    uint8_t       spatial_count         = 0;
    uint8_t       event_count           = 0;
    uint8_t       _pad[2]               = {};

    // Add or update spatial observation. Evicts oldest slot when full.
    void AddSpatial(float wx, float wz, uint32_t now_ms, uint8_t conf = 200) noexcept {
        uint8_t slot = spatial_count < MAX_SPATIAL ? spatial_count++ : 0u;
        if (spatial_count == MAX_SPATIAL) {
            // LRU evict: find oldest
            uint32_t oldest_ms = spatial[0].timestamp_ms;
            slot = 0;
            for (uint8_t i = 1; i < MAX_SPATIAL; ++i) {
                if (spatial[i].timestamp_ms < oldest_ms) {
                    oldest_ms = spatial[i].timestamp_ms;
                    slot = i;
                }
            }
        }
        spatial[slot] = {wx, wz, now_ms, conf, {}};
    }

    // Record a witnessed event by FNV ID. Ring-overwrites oldest.
    void AddEvent(uint32_t event_id) noexcept {
        if (event_count < MAX_EVENTS) {
            events[event_count++] = event_id;
        } else {
            // shift left, append
            for (uint8_t i = 0; i < MAX_EVENTS - 1; ++i)
                events[i] = events[i + 1];
            events[MAX_EVENTS - 1] = event_id;
        }
    }

    bool HasEvent(uint32_t event_id) const noexcept {
        for (uint8_t i = 0; i < event_count; ++i)
            if (events[i] == event_id) return true;
        return false;
    }

    // Drop spatial memories older than max_age_ms.
    void ExpireOlderThan(uint32_t now_ms, uint32_t max_age_ms) noexcept {
        uint8_t w = 0;
        for (uint8_t r = 0; r < spatial_count; ++r) {
            if (now_ms - spatial[r].timestamp_ms <= max_age_ms)
                spatial[w++] = spatial[r];
        }
        spatial_count = w;
    }

    void ClearAll() noexcept {
        spatial_count = 0;
        event_count   = 0;
    }
};
