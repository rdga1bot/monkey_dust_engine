#pragma once
#include <cstdint>
#include <cmath>

// CoverSystem — B-4: spatial registry of cover points for BT nodes.
// BT nodes (ConditionIsInCover, ActionIdleInCover, etc.) already exist in
// behavior_tree.h and use lcf::IS_IN_COVER flag. This provides the spatial
// query layer: "find nearest valid cover relative to a threat position."
//
// Max 256 cover points per zone; flat array with linear search (covers are sparse).
// RegisterCoverPoint() called when building/rock entities spawn.
// FindNearestCover() used by actMoveToCover BT action.

static constexpr int MAX_COVER_POINTS = 256;

struct CoverPoint {
    float   x, z;          // world position (centre)
    float   facing_rad;    // direction the cover faces (away from threat side)
    uint8_t quality;       // 0=poor, 255=excellent (height + solidity estimate)
    uint8_t _pad[3];
};
static_assert(sizeof(CoverPoint) == 16, "CoverPoint must be 16 bytes");

class CoverSystem {
public:
    static CoverSystem& Get() noexcept { static CoverSystem s; return s; }

    void Clear() noexcept { count_ = 0; }

    bool RegisterCoverPoint(float x, float z, float facing_rad,
                             uint8_t quality = 128) noexcept {
        if (count_ >= MAX_COVER_POINTS) return false;
        points_[count_++] = { x, z, facing_rad, quality };
        return true;
    }

    void UnregisterCoverPoint(float x, float z) noexcept {
        for (int i = 0; i < count_; ++i) {
            if (fabsf(points_[i].x - x) < 0.5f && fabsf(points_[i].z - z) < 0.5f) {
                points_[i] = points_[--count_];
                return;
            }
        }
    }

    // Find nearest cover point within radius_m that faces away from the threat.
    // threat_x/z: position of the enemy the NPC is hiding from.
    // Returns nullptr if no suitable cover found.
    const CoverPoint* FindNearestCover(float from_x, float from_z,
                                        float threat_x, float threat_z,
                                        float radius_m = 20.f) const noexcept {
        float r2 = radius_m * radius_m;
        // Threat direction (threat → NPC): cover should face this direction
        float tdx = from_x - threat_x, tdz = from_z - threat_z;
        float t_angle = atan2f(tdx, tdz);

        const CoverPoint* best = nullptr;
        float best_score = -1.f;
        for (int i = 0; i < count_; ++i) {
            const CoverPoint& cp = points_[i];
            float dx = cp.x - from_x, dz = cp.z - from_z;
            float d2 = dx*dx + dz*dz;
            if (d2 > r2) continue;
            // Prefer cover that faces the threat and is high quality
            float angle_diff = fabsf(cp.facing_rad - t_angle);
            if (angle_diff > 3.14159f) angle_diff = 6.28318f - angle_diff;
            float facing_score = 1.f - angle_diff / 3.14159f; // 1=perfect, 0=facing wrong way
            float dist_score   = 1.f - sqrtf(d2) / radius_m;
            float quality_score= (float)cp.quality / 255.f;
            float score = facing_score * 0.5f + dist_score * 0.3f + quality_score * 0.2f;
            if (score > best_score) { best_score = score; best = &cp; }
        }
        return best;
    }

    int Count() const noexcept { return count_; }

private:
    CoverSystem() noexcept : count_(0) {}
    CoverPoint points_[MAX_COVER_POINTS];
    int        count_;
};
