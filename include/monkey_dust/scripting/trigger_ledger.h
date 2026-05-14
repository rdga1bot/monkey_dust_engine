#pragma once
#include <cstdint>
#include <monkey_dust/ai/fnv.h>

// TriggerLedger — MD TriggerInfo pattern adapted as a persistent
// ring buffer of trigger events for replay/save context (M48).
//
// Each TriggerRecord is a 16-byte POD storing: FNV-1a trigger ID,
// entity sender (raw uint32), timestamp (ms), and duration (ms).
// MAX_ENTRIES is the ring capacity; oldest entry is overwritten.
// "Immutable" in the MD sense: past entries are never mutated,
// only the write head advances.

static constexpr uint8_t TRIGGER_LEDGER_MAX = 32;

struct TriggerRecord {
    uint32_t trigger_id;   // fnv1a of trigger name
    uint32_t sender;       // entity id (raw uint32)
    uint32_t timestamp_ms;
    uint32_t duration_ms;
};
static_assert(sizeof(TriggerRecord) == 16);

class TriggerLedger {
public:
    static TriggerLedger& Get() noexcept {
        static TriggerLedger inst;
        return inst;
    }

    // Append a trigger event. Returns index of the new entry.
    uint8_t Push(uint32_t trigger_id, uint32_t sender,
                 uint32_t timestamp_ms, uint32_t duration_ms = 0) noexcept {
        uint8_t idx        = head_;
        buf_[idx]          = { trigger_id, sender, timestamp_ms, duration_ms };
        head_              = (head_ + 1u) % TRIGGER_LEDGER_MAX;
        if (count_ < TRIGGER_LEDGER_MAX) ++count_;
        ++seq_;
        return idx;
    }

    uint8_t Push(const char* name, uint32_t sender,
                 uint32_t timestamp_ms, uint32_t duration_ms = 0) noexcept {
        return Push(md::fnv1a_rt(name), sender, timestamp_ms, duration_ms);
    }

    // Most-recent N entries (0 = newest), clamped to count().
    const TriggerRecord* Latest(uint8_t offset = 0) const noexcept {
        if (offset >= count_) return nullptr;
        uint8_t idx = (head_ + TRIGGER_LEDGER_MAX - 1u - offset) % TRIGGER_LEDGER_MAX;
        return &buf_[idx];
    }

    // Walk all stored entries newest-first.
    // Fn signature: void(const TriggerRecord&)
    template<typename Fn>
    void ForEach(Fn&& fn) const noexcept {
        for (uint8_t i = 0; i < count_; ++i) {
            uint8_t idx = (head_ + TRIGGER_LEDGER_MAX - 1u - i) % TRIGGER_LEDGER_MAX;
            fn(buf_[idx]);
        }
    }

    // Find most-recent entry with matching trigger_id. nullptr if not found.
    const TriggerRecord* FindLast(uint32_t trigger_id) const noexcept {
        for (uint8_t i = 0; i < count_; ++i) {
            uint8_t idx = (head_ + TRIGGER_LEDGER_MAX - 1u - i) % TRIGGER_LEDGER_MAX;
            if (buf_[idx].trigger_id == trigger_id) return &buf_[idx];
        }
        return nullptr;
    }

    // Count entries matching a trigger_id.
    uint8_t CountOf(uint32_t trigger_id) const noexcept {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count_; ++i) {
            uint8_t idx = (head_ + TRIGGER_LEDGER_MAX - 1u - i) % TRIGGER_LEDGER_MAX;
            n += (buf_[idx].trigger_id == trigger_id) ? 1u : 0u;
        }
        return n;
    }

    void     Clear()  noexcept { head_ = 0; count_ = 0; seq_ = 0; }
    uint8_t  count()  const noexcept { return count_; }
    uint32_t seq()    const noexcept { return seq_; }  // monotonic push counter

private:
    TriggerRecord buf_[TRIGGER_LEDGER_MAX] = {};
    uint8_t  head_  = 0;
    uint8_t  count_ = 0;
    uint32_t seq_   = 0;
};
