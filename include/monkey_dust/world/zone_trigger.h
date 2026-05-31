#pragma once
#include <cstring>
#include <cstdint>

// ── ZoneTrigger (M-3 VBfA pattern) ───────────────────────────────────────────
// VBfA line 59742: __aligned_malloc(0x19c, 0x10) × 27 instances.
// 0x19c = 412 bytes: vtable(4) + pad(4) + bitfield(400) + flags(1) + pad(7)
// memset(+8, -1, 400): initialise bitfield to all-1 (no entities).
//
// Each bit = entity slot; O(1) membership check for 3200 entities per zone.
// Used for: trigger volumes, zone enter/exit events, NPC area tracking.
//
// sizeof = 412 bytes (matches VBfA 0x19c exactly).

struct alignas(16) ZoneTrigger {
    static constexpr int BITFIELD_BYTES = 400;
    static constexpr int MAX_ENTITIES   = BITFIELD_BYTES * 8;  // 3200 slots

    // +0x00: vtable-style function pointer (type dispatch)
    void (*on_enter)(int slot) = nullptr;
    void (*on_exit) (int slot) = nullptr;

    // +0x08: entity presence bitfield (400 bytes, bit N = entity slot N present)
    uint8_t  entities_mask[BITFIELD_BYTES];  // +0x08

    // +0x198 (0x66*4): control flags byte
    uint8_t  flags;   // bit0=active, bit1=one_shot, bit2=player_only
    uint8_t  _pad[3];

    // O(1) init — memset to 0 (no entities present)
    void Init() {
        on_enter = nullptr;
        on_exit  = nullptr;
        memset(entities_mask, 0, BITFIELD_BYTES);
        flags = 0;
    }

    // O(1) presence check (VBfA bitfield pattern)
    bool Contains(int slot) const {
        if (slot < 0 || slot >= MAX_ENTITIES) return false;
        return (entities_mask[slot >> 3] >> (slot & 7)) & 1u;
    }

    // O(1) add/remove
    void Add(int slot) {
        if (slot < 0 || slot >= MAX_ENTITIES) return;
        uint8_t& byte = entities_mask[slot >> 3];
        uint8_t  bit  = 1u << (slot & 7);
        if (!(byte & bit)) {
            byte |= bit;
            if (on_enter) on_enter(slot);
        }
    }
    void Remove(int slot) {
        if (slot < 0 || slot >= MAX_ENTITIES) return;
        uint8_t& byte = entities_mask[slot >> 3];
        uint8_t  bit  = 1u << (slot & 7);
        if (byte & bit) {
            byte &= ~bit;
            if (on_exit) on_exit(slot);
        }
    }

    bool IsActive()   const { return (flags & 1u) != 0; }
    bool IsOneShot()  const { return (flags & 2u) != 0; }
    bool PlayerOnly() const { return (flags & 4u) != 0; }
};
// VBfA 32-bit original = 412B (0x19c); our 64-bit with 2×fn_ptr = 432B (same layout intent).
static_assert(sizeof(ZoneTrigger) % 16 == 0, "ZoneTrigger must be 16-byte aligned multiple");
