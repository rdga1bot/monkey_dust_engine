#pragma once
#include <cstdint>

// ── VentRegistry ──────────────────────────────────────────────────────────────
// Header-only singleton. Stores world positions (x, z) for up to MAX_VENTS=8
// vents. Used by ConditionHasVentCloseToAlien and ConditionHasFlankedVentCloseToPlayer
// BT conditions. Game code calls Register() when vents spawn; ClearAll() on level reset.
struct VentEntry {
    float   x, z;
    uint8_t id;
    uint8_t _pad[3];
};

class VentRegistry {
public:
    static constexpr uint8_t MAX_VENTS        = 8;
    static constexpr float   DEFAULT_RADIUS_M = 6.f;

    static VentRegistry& Get() {
        static VentRegistry instance;
        return instance;
    }

    // Register or update a vent position (re-registers same id updates coords).
    void Register(uint8_t id, float x, float z) {
        for (uint8_t i = 0; i < count_; ++i) {
            if (vents_[i].id == id) { vents_[i].x = x; vents_[i].z = z; return; }
        }
        if (count_ >= MAX_VENTS) return;
        vents_[count_++] = {x, z, id, {0, 0, 0}};
    }

    void    ClearAll() { count_ = 0; }
    uint8_t Count()    const { return count_; }

    // Returns true if any registered vent is within radius_m of (x, z).
    bool AnyWithinRadius(float x, float z, float radius_m) const {
        float r2 = radius_m * radius_m;
        for (uint8_t i = 0; i < count_; ++i) {
            float dx = vents_[i].x - x;
            float dz = vents_[i].z - z;
            if (dx * dx + dz * dz <= r2) return true;
        }
        return false;
    }

private:
    VentRegistry() { ClearAll(); }
    VentEntry vents_[MAX_VENTS];
    uint8_t   count_ = 0;
};
