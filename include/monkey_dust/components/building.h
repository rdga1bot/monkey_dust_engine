#pragma once
#include <monkey_dust/building/production_chain.h>
#include <cstdint>

// Kenshi RE: mesh_states = blueprint / under_construction / built / destroyed.
enum class BuildingPhase : uint8_t {
    Blueprint        = 0,   // placed, not yet built; cannot be entered
    UnderConstruction= 1,   // being built; workers add progress each tick
    Built            = 2,   // fully functional
    Damaged          = 3,   // HP < 30% — reduced function
    Destroyed        = 4,   // rubble; needs removal before rebuild
};

// Construction progress at which phase transitions occur:
// 0%→UnderConstruction (immediate on place), 100%→Built.
static constexpr float BUILD_PROGRESS_FULL = 100.f;
static constexpr float BUILD_WORKER_RATE   = 5.f;  // % per second per worker

struct Building {
    uint32_t        def_id       = 0;
    int             grid_x       = 0, grid_z = 0;
    int             size_x       = 1, size_z = 1;
    ProductionChain chain        = {};
    float           progress_s   = 0.f;   // production progress (existing)
    bool            active       = false;

    // ── BuildingState ──────────────────────────────────────────────────────
    BuildingPhase   phase        = BuildingPhase::Blueprint;
    float           build_pct    = 0.f;   // 0..100 construction progress
    float           hp           = 0.f;   // current structural HP
    float           hp_max       = 500.f; // from BuildingDef
    float           power_output = 0.f;   // >0 = generator; <0 = consumer (cats/s)
    uint8_t         _pad[4]      = {};

    bool IsBuilt()    const noexcept { return phase == BuildingPhase::Built; }
    bool IsBlueprint()const noexcept { return phase == BuildingPhase::Blueprint; }

    // Advance construction by one worker's contribution over dt seconds.
    // Returns true when build completes.
    bool TickConstruct(float dt) noexcept {
        if (phase != BuildingPhase::UnderConstruction) return false;
        build_pct += BUILD_WORKER_RATE * dt;
        if (build_pct >= BUILD_PROGRESS_FULL) {
            build_pct = BUILD_PROGRESS_FULL;
            phase     = BuildingPhase::Built;
            active    = true;
            return true;
        }
        return false;
    }

    // Apply structural damage; transitions to Damaged/Destroyed if needed.
    void TakeDamage(float dmg) noexcept {
        if (phase != BuildingPhase::Built && phase != BuildingPhase::Damaged) return;
        hp -= dmg;
        if (hp <= 0.f) { hp = 0.f; phase = BuildingPhase::Destroyed; active = false; }
        else if (hp < hp_max * 0.3f) phase = BuildingPhase::Damaged;
    }
};
