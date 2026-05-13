#pragma once
#include <monkey_dust/components/sense_component.h>
#include <cstdint>

// ── SenseRegistry ─────────────────────────────────────────────────────────────
// Singleton. Loads view_cone_sets.json and holds named ViewConeSet entries.
// SenseComponent::cone_set_idx is a direct index into sets[].
// CATHODE analog: VIEW_CONE_SETS/ directory — 6 named perception profiles.

static constexpr int MAX_VIEW_CONE_TYPES = 4;  // Close/Focused/Normal/Peripheral
static constexpr int MAX_CONE_SETS       = 8;

struct ViewConeSet {
    char     name[24];
    ViewCone cones[MAX_VIEW_CONE_TYPES];
    float    threshold_lo;
    float    threshold_hi;
    uint8_t  cone_count;
    uint8_t  _pad[3];
};

class SenseRegistry {
public:
    static SenseRegistry& Get();

    // Returns false and logs a warning if path missing or parse error.
    bool Load(const char* json_path);

    // Returns nullptr if not found (never assert — log warning + return default).
    const ViewConeSet* Find(const char* name) const;

    // Direct index access for SenseComponent::cone_set_idx.
    // Returns nullptr for out-of-range idx; callers must guard.
    const ViewConeSet* At(uint8_t idx) const;

    uint8_t Count() const { return count_; }

private:
    ViewConeSet sets_[MAX_CONE_SETS] = {};
    uint8_t     count_               = 0;
};
