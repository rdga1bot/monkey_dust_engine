#pragma once
#include <cstdint>
#include <cstring>

// VfxPool — pre-allocated ring buffer for lightweight world effects.
//
// VBfA RE §8.4: _vector_constructor_iterator_ pre-allocates:
//   - 8000 generic objects at 96B = 768 KB
//   - 2000 lightweight effects at 12B = 24 KB
//
// VfxEffect = 12B (matches VBfA lightweight slot):
//   pos(8B) + type(1B) + life(1B) + pad(2B)
//
// All effects pre-allocated at startup. No malloc during gameplay.
// Spawn = write into next ring slot; Tick = decrement life; at 0 = slot reused.

enum class VfxType : uint8_t {
    None      = 0,
    BloodPuff = 1,  // melee hit — red smoke burst
    DustPuff  = 2,  // footstep / landing — grey dust
    Spark     = 3,  // blade clash / bullet hit
    Smoke     = 4,  // fire / explosion residue
    Heal      = 5,  // bandage / healing indicator
};

struct VfxEffect {
    float    x, y;      // world XZ position (4+4 = 8B)
    VfxType  type;      // effect type
    uint8_t  life;      // remaining life ticks (0 = dead, 255 = ~25 s at 10 TPS)
    uint8_t  _pad[2];
};                      // = 12B exactly (VBfA lightweight slot size)
static_assert(sizeof(VfxEffect) == 12, "VfxEffect must be 12 bytes (VBfA lightweight slot)");

// VfxPool — 2000 pre-allocated slots. Matches VBfA 2000-effect pool.
// Not thread-safe; call Spawn()/Tick() from logic thread only.
class VfxPool {
public:
    static constexpr int MAX_EFFECTS = 2000;  // VBfA RE: 2000 lightweight objects

    static VfxPool& Get() {
        static VfxPool inst;
        return inst;
    }

    // Spawn a new effect. Overwrites oldest if pool full (ring semantics).
    void Spawn(float wx, float wz, VfxType type, uint8_t life_ticks = 30) noexcept {
        slots_[head_] = { wx, wz, type, life_ticks, {0,0} };
        head_ = (head_ + 1) % MAX_EFFECTS;
        if (active_ < MAX_EFFECTS) ++active_;
    }

    // Decrement life on all active effects (call once per logic tick).
    void Tick() noexcept {
        for (int i = 0; i < MAX_EFFECTS; ++i) {
            if (slots_[i].life > 0) --slots_[i].life;
        }
    }

    const VfxEffect* Slots() const noexcept { return slots_; }
    int Active()           const noexcept { return active_; }

private:
    VfxPool() {
        memset(slots_, 0, sizeof(slots_));
    }
    // VBfA RE: pre-allocated at startup via _vector_constructor_iterator_ (no malloc at runtime).
    VfxEffect slots_[MAX_EFFECTS];
    int       head_   = 0;
    int       active_ = 0;
};

// GenericObjectPool — 8000 pre-allocated 96B slots for logic objects.
// VBfA RE §8.4: _vector_constructor_iterator_(&DAT_00826a90, 0x60, 8000, ctor).
// (0x60 = 96 bytes per slot). Use for: combat events, sound triggers, callbacks.
// Separate used[] bitset keeps stride exactly 96B (no bool padding).
struct alignas(16) GenericSlot {
    uint8_t data[96];  // exactly 96B = 0x60 (VBfA stride)
};
static_assert(sizeof(GenericSlot) == 96, "GenericSlot must be 96 bytes (VBfA 0x60 stride)");

class GenericObjectPool {
public:
    static constexpr int MAX_SLOTS = 8000;  // VBfA RE: 8000 generic objects

    static GenericObjectPool& Get() {
        static GenericObjectPool inst;
        return inst;
    }

    // Acquire a free slot (nullptr if all 8000 in use).
    GenericSlot* Acquire() noexcept {
        for (int i = 0; i < MAX_SLOTS; ++i) {
            int idx = (last_ + i) % MAX_SLOTS;
            if (!used_[idx]) {
                used_[idx] = true;
                last_ = (idx + 1) % MAX_SLOTS;
                return &slots_[idx];
            }
        }
        return nullptr;  // pool exhausted
    }

    void Release(GenericSlot* s) noexcept {
        if (!s) return;
        int idx = (int)(s - slots_);
        if (idx >= 0 && idx < MAX_SLOTS) used_[idx] = false;
    }

private:
    GenericObjectPool() {
        memset(slots_, 0, sizeof(slots_));
        memset(used_,  0, sizeof(used_));
    }
    GenericSlot slots_[MAX_SLOTS];   // 8000 × 96B = 768 KB (pre-alloc, no runtime malloc)
    bool        used_[MAX_SLOTS];    // separate bitset keeps GenericSlot stride clean
    int         last_ = 0;
};
