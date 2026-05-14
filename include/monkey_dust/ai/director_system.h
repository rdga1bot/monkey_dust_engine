#pragma once
#include <cstdint>

// ── DirectorStage ─────────────────────────────────────────────────────────────
// Stages are driven by menace_ [0..1]; thresholds: 0.25 / 0.50 / 0.75.
enum class DirectorStage : uint8_t {
    Unaware,    // menace < 0.25
    Suspicious, // 0.25..0.50
    Hunting,    // 0.50..0.75
    Intense     // > 0.75
};

// ── DirectorProfile ───────────────────────────────────────────────────────────
// Loaded from data/ai/director_profiles.json.
struct DirectorProfile {
    char  name[24];
    float gauge_fill_rate;    // menace/sec while threat perceived (=1/gauge_fill_s)
    float hunt_timeout_min_s; // min seconds in Hunting before switching strategy
    float hunt_timeout_max_s; // max seconds
    float ambush_wait_s;      // seconds to wait at ambush position
    float trap_trigger_s;     // seconds until killtrap fires
    float sweep_radius_m;     // search radius in world units
    int   max_menaces;        // max simultaneous threat sources (Alien=1)
};

// ── DirectorSystem ────────────────────────────────────────────────────────────
// Singleton. Reads SenseComponent.activation from all NPC entities,
// accumulates menace gauge, updates DirectorStage, and broadcasts
// "menace" (float) and "director_stage" (int) to every AgentBlackboard.
//
// Tick frequency: called from game logic tick (10 TPS → dt ≈ 0.1f).
// Init() must be called before any Tick(); passing a missing path is non-fatal.
class DirectorSystem {
public:
    static DirectorSystem& Get();

    void  Init(const char* profiles_json_path);
    void  Tick(float dt);

    // Switch active profile by name; logs warning if not found (keeps current).
    void  SetProfile(const char* name);

    float         GetMenace() const { return menace_; }
    DirectorStage GetStage()  const { return stage_;  }

    const DirectorProfile* GetCurrentProfile() const;

private:
    DirectorSystem() = default;

    static constexpr int MAX_PROFILES = 8;

    float         menace_       = 0.f;
    DirectorStage stage_        = DirectorStage::Unaware;
    int           profile_idx_  = 0;
    int           profile_count_= 0;
    DirectorProfile profiles_[MAX_PROFILES] = {};
};
