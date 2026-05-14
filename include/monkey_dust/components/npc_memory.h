#pragma once
#include <cstdint>

// ── SuspiciousItemStage ───────────────────────────────────────────────────────
// CATHODE SUSPICIOUS_ITEM_STAGE — NPC investigation phase for a witnessed item.
// Stored in NpcEventRecord::investigation_stage; updated by AI response system.
enum class SuspiciousItemStage : uint8_t {
    None                          = 0,
    FirstSensed                   = 1,
    InitialReaction               = 2,
    WaitForTeamMembersRouting     = 3,
    MoveCloseTo                   = 4,
    CloseToReaction               = 5,
    CloseToWaitForGroupMembers    = 6,
    SearchArea                    = 7,
    Unknown                       = 0xFF,
};

// ── SuspiciousItemType ────────────────────────────────────────────────────────
// CATHODE SUSPICIOUS_ITEM enum — typed stimulus categories for NPC event memory.
// Replaces raw uint32_t FNV IDs: structured type lets BT nodes gate on stimulus class.
enum class SuspiciousItemType : uint8_t {
    Explosion        =  0,
    DeadBody         =  1,
    Alarm            =  2,
    VentHiss         =  3,
    ActiveFlare      =  4,
    CutPanel         =  5,
    FireExtinguisher =  6,
    LightsOn         =  7,
    LightsOff        =  8,
    DoorOpened       =  9,
    DoorClosed       = 10,
    DiscardedGun     = 11,
    DeadFlare        = 12,
    GlowStick        = 13,
    OpenContainer    = 14,
    NoiseMaker       = 15,
    EmpMine          = 16,
    FlashBang        = 17,
    Molotov          = 18,
    PipeBomb         = 19,
    SmokeBomb        = 20,
    Unknown          = 0xFF,
};

// ── NpcEventRecord ────────────────────────────────────────────────────────────
// Typed event entry — 16 bytes, same size as SpatialMemory for cache alignment.
// intensity maps to ff::SUSPICIOUS_ITEM_*_PRIORITY (0=low, 1=medium, 2=high).
struct NpcEventRecord {
    uint32_t            timestamp_ms        = 0;
    float               x                   = 0.f;  // approximate source position
    float               z                   = 0.f;
    SuspiciousItemType  type                = SuspiciousItemType::Unknown;
    uint8_t             intensity           = 0;    // 0=low, 1=medium, 2=high
    SuspiciousItemStage investigation_stage = SuspiciousItemStage::None;
    uint8_t             _pad[1]             = {};
};
static_assert(sizeof(NpcEventRecord) == 16, "NpcEventRecord must be 16 bytes");

// ── NpcMemoryComponent ────────────────────────────────────────────────────────
// Echo-inspired autonomous NPC memory: spatial observations + typed event history.
// Flat POD arrays — no heap. One component per NPC entity.
//
// Spatial memories: last-seen positions of targets/threats (8 slots, LRU evict).
// Event memories: typed stimulus events witnessed by this NPC (8 slots, ring).
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

    SpatialMemory  spatial[MAX_SPATIAL] = {};
    NpcEventRecord events [MAX_EVENTS]  = {};
    uint8_t        spatial_count        = 0;
    uint8_t        event_count          = 0;
    uint8_t        _pad[2]              = {};

    void AddSpatial(float wx, float wz, uint32_t now_ms, uint8_t conf = 200) noexcept {
        uint8_t slot = spatial_count < MAX_SPATIAL ? spatial_count++ : 0u;
        if (spatial_count == MAX_SPATIAL) {
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

    void AddEvent(SuspiciousItemType type, uint32_t timestamp_ms = 0,
                  float x = 0.f, float z = 0.f, uint8_t intensity = 0) noexcept {
        NpcEventRecord rec;
        rec.timestamp_ms = timestamp_ms;
        rec.x            = x;
        rec.z            = z;
        rec.type         = type;
        rec.intensity    = intensity;
        if (event_count < MAX_EVENTS) {
            events[event_count++] = rec;
        } else {
            for (uint8_t i = 0; i < MAX_EVENTS - 1; ++i)
                events[i] = events[i + 1];
            events[MAX_EVENTS - 1] = rec;
        }
    }

    bool HasEventOfType(SuspiciousItemType type) const noexcept {
        for (uint8_t i = 0; i < event_count; ++i)
            if (events[i].type == type) return true;
        return false;
    }

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
