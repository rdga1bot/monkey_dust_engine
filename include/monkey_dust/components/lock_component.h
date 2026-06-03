#pragma once
#include <cstdint>

// LockComponent — B-2: door/container lock state.
// Kenshi RE: lock_level 1-5 × 20% base block; thievery skill rolls per tick.
// Attach to building/container entities that can be locked.

static constexpr int   MAX_LOCK_LEVEL      = 5;
static constexpr float LOCKPICK_TICK_RATE  = 0.10f; // progress/s per thievery skill point

struct LockComponent {
    uint32_t owner_faction_id  = 0;  // 0 = world (no owner penalty)
    uint8_t  lock_level        = 1;  // 1–5: difficulty (0 = unlocked permanently)
    uint8_t  is_locked         = 1;  // 1 = locked; 0 = unlocked
    uint8_t  lockpick_progress = 0;  // 0–100; reaches 100 → is_locked = 0
    uint8_t  _pad              = 0;
};
static_assert(sizeof(LockComponent) == 8, "LockComponent must be 8 bytes");

// Kenshi: base_block_chance = lock_level × 0.20; thievery skill reduces it.
// Returns lock pick progress increment (0..1 per tick) given skill [0..100].
inline float LockpickTickGain(uint8_t thievery_skill, uint8_t lock_level) noexcept {
    float block = (float)lock_level * 0.20f;                  // 0.20–1.00
    float roll  = (float)thievery_skill * LOCKPICK_TICK_RATE; // 0–10
    float gain  = roll * (1.f - block);
    return gain < 0.f ? 0.f : gain;
}
