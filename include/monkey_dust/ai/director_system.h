#pragma once
#include <cstdint>
#include <monkey_dust/ai/alien_config.h>
#include <monkey_dust/components/agent_state.h>

// ── DirectorStage ─────────────────────────────────────────────────────────────
// Stages are driven by menace_ [0..1]; thresholds: 0.25 / 0.50 / 0.75.
enum class DirectorStage : uint8_t {
    Unaware,    // menace < 0.25
    Suspicious, // 0.25..0.50
    Hunting,    // 0.50..0.75
    Intense     // > 0.75
};

// ── ThreatState ───────────────────────────────────────────────────────────────
// 6 discrete states confirmed by AI.exe RE (FUN_008b6770 string table).
// More granular than DirectorStage: DirectorStage drives menace fill logic,
// ThreatState drives per-NPC animation selection and BT branch choice.
// Mapping: MenaceToThreatState() below converts float menace → ThreatState.
enum class ThreatState : uint8_t {
    Neutral                    = 0,  // NEUTRAL — no threat awareness
    Suspicious                 = 1,  // SUSPICIOUS — investigating
    Aggressive                 = 2,  // AGGRESSIVE — direct attack
    Panicked                   = 3,  // PANICKED — fleeing
    ThreatEscalationAggressive = 4,  // THREAT_ESCALATION_AGGRESSIVE — full assault
    ThreatEscalationPanicked   = 5,  // THREAT_ESCALATION_PANICKED — mass retreat
};

// Convert float menace [0..1] to ThreatState.
// is_panicking: set true for NPCs with low Toughness or injured limbs.
inline ThreatState MenaceToThreatState(float menace, bool is_panicking = false) noexcept {
    if (menace < 0.25f) return ThreatState::Neutral;
    if (menace < 0.55f) return ThreatState::Suspicious;
    if (menace < 0.85f) return is_panicking ? ThreatState::Panicked : ThreatState::Aggressive;
    return is_panicking
        ? ThreatState::ThreatEscalationPanicked
        : ThreatState::ThreatEscalationAggressive;
}

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
    int   max_menaces;        // max simultaneous threat sources (default=1)
};

// ── DirectorConfig ────────────────────────────────────────────────────────────
// AI-1: full 26-field Director tuning config (Kenshi/VBfA-R AI RE).
// One instance is active at runtime; loaded from data/ai/director_config.json.
struct DirectorConfig {
    // ── Menace / stage thresholds ─────────────────────────────────────────────
    // RE-calibrated from AI.exe.c (VBfA): fill_s=8, decay_s=12, cooldown_s=5
    float menace_fill_rate        = 0.125f; // menace/s while threat active (1/8)
    float menace_drain_rate       = 0.083f; // menace/s while safe (1/12)
    float menace_cooldown_s       = 5.0f;   // pause between escalations (RE: 5s)
    int   max_menaces             = 3;      // max simultaneous threat sources (RE: 3)
    float stage_suspicious_thresh = 0.25f;  // menace ≥ this → Suspicious
    float stage_hunting_thresh    = 0.50f;  // menace ≥ this → Hunting
    float stage_intense_thresh    = 0.75f;  // menace ≥ this → Intense
    float escalation_mult         = 2.0f;   // fill rate multiplier during combat

    // ── Spawn / despawn ───────────────────────────────────────────────────────
    float spawn_radius_m          = 80.0f;  // max dist from player to spawn threats
    float despawn_radius_m        = 150.0f; // dist at which threats despawn
    int   max_spawns_per_wave     = 4;      // entities per spawn wave
    float spawn_cooldown_s        = 30.0f;  // seconds between waves
    int   max_active_threats      = 16;     // hard cap on active director-spawned entities

    // ── Search / stalk ────────────────────────────────────────────────────────
    // RE-calibrated: near_target_min=5m, stalk_radius=20m, stalk_timeout=30s
    float near_target_min_m       = 5.0f;   // stalk exclusion radius (RE: 5m)
    float stalk_radius_m          = 20.0f;  // stalk movement radius (RE: 20m)
    float stalk_timeout_s         = 30.0f;  // abandon stalk after this (RE: 30s)
    float chase_break_time_s      = 15.0f;  // give up chase after this duration
    float search_timeout_s        = 45.0f;  // total search budget before de-escalate
    float search_grid_size_m      = 10.0f;  // grid cell size for systematic search
    float patrol_radius_m         = 40.0f;  // default patrol wander radius
    float alert_propagation_range_m = 30.0f;// range at which alert spreads to allies

    // ── Combat behaviour ─────────────────────────────────────────────────────
    float flee_hp_fraction        = 0.25f;  // HP fraction below which NPCs flee
    float backup_request_range_m  = 50.0f;  // range to call for backup
    float ambush_setup_time_s     = 5.0f;   // time to take position before ambush fires
    float trap_trigger_s          = 3.0f;   // seconds until killtrap fires after setup

    // ── Awareness decay ───────────────────────────────────────────────────────
    float awareness_decay_s       = 20.0f;  // seconds for full awareness to decay
    float hearing_decay_s         = 8.0f;   // seconds for sound memory to expire
    float visual_decay_s          = 5.0f;   // seconds for visual memory to expire

    // ── Director pacing ───────────────────────────────────────────────────────
    float min_quiet_s             = 60.0f;  // min quiet time before next escalation
    float max_idle_menace         = 0.10f;  // menace kept > 0 even when all clear
    float director_tick_s         = 2.0f;   // director update interval (s)
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
    bool  LoadConfig(const char* config_json_path);
    void  Tick(float dt);

    // Switch active profile by name; logs warning if not found (keeps current).
    void  SetProfile(const char* name);

    float         GetMenace() const { return menace_; }
    DirectorStage GetStage()  const { return stage_;  }

    // Batch 10 P9: BT-driven menace override. mode: 0=add, 1=set, 2=subtract. Clamps [0..1].
    void AdjustMenace(float delta, uint8_t mode = 0);

    // M52: Select the best motivation for an agent using UtilityScorer.
    // Writes chosen motivation to as.motivation.
    // Increments as.motivation_ticks when motivation stays the same;
    // resets to 0 when it changes (used by UtilityScorer inertia Bias).
    // Call once per logic tick per NPC, after Tick().
    MotivationType ChooseMotivation(AgentState& as, uint32_t now_ms) noexcept;

    const DirectorProfile*  GetCurrentProfile() const;
    const DirectorConfig&   GetConfig() const { return config_; }
    void                    SetConfig(const DirectorConfig& c) { config_ = c; }

    // Batch 12 P6: active AlienConfigPreset tracking for ConditionAlienIsAllowed.
    AlienConfigurationType GetActiveConfig() const { return active_config_; }
    void SetActiveConfig(AlienConfigurationType c) { active_config_ = c; }

    // ── VBfA Alliance Group Priority Scheduling ───────────────────────────────
    // VBfA lines 188388-188427: 18 groups, priority 250→10 (−16/group), cap 200 units.
    // Higher priority factions processed first — foreground battle always wins CPU time.
    static constexpr int   MAX_FACTION_GROUPS   = 18;
    static constexpr int   GROUP_PRIORITY_START = 250;
    static constexpr int   GROUP_PRIORITY_STEP  = 16;
    static constexpr int   GROUP_UNIT_CAP       = 200;

    struct FactionGroupBudget {
        uint32_t faction_id   = 0;
        int      priority     = 0;   // 250 → 10, decrements by 16 per group
        int      unit_cap     = GROUP_UNIT_CAP;
        int      units_active = 0;   // reset each frame
    };

    // Call each frame to assign priorities. faction_ids[] sorted by importance (index=priority rank).
    void RebuildGroupBudgets(const uint32_t* faction_ids, int count);
    // Returns true if this faction is under its per-group unit cap.
    bool TryActivateUnit(uint32_t faction_id);
    // Reset active counts at frame start (call before Tick).
    void ResetGroupCounts();
    // Get budget table for external read (e.g. editor overlay).
    const FactionGroupBudget* GroupBudgets() const { return group_budgets_; }
    int GroupCount() const { return group_count_; }

private:
    DirectorSystem() = default;

    static constexpr int MAX_PROFILES = 8;

    DirectorConfig config_;
    float         menace_       = 0.f;
    DirectorStage stage_        = DirectorStage::Unaware;
    AlienConfigurationType active_config_ = AlienConfigurationType::Default;
    int           profile_idx_  = 0;
    int           profile_count_= 0;
    DirectorProfile profiles_[MAX_PROFILES] = {};

    // Last-broadcast values: skip AgentState iteration when nothing changed.
    float         last_bc_menace_ = -1.f;
    DirectorStage last_bc_stage_  = DirectorStage::Unaware;

    // VBfA alliance group priority scheduling data
    FactionGroupBudget group_budgets_[MAX_FACTION_GROUPS] = {};
    int                group_count_ = 0;
};
