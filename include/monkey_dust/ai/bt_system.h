#pragma once
#include <monkey_dust/ecs/engine_context.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/bt_components.h>
#include <entt/entt.hpp>
#include <cstdint>

// ── BTSystem ──────────────────────────────────────────────────────────────────
// Main BT execution loop.
// Call Tick() once per logic tick (10 TPS).
//
// Per-entity sequence:
//   1. Clear AgentState::frame_flags (C13 invariant — single-tick signals reset)
//   2. Tick DirectorHintComponent: expire stale hints (>MAX_PENDING_TICKS)
//   3. Tiered LOD skip (VBfA/CATHODE RE): entities with motivation==Dormant or
//      distance > TIER_FAR_M are skipped every N frames (every 5/10/20 logic ticks).
//   4. If BehaviorTreeComponent::enabled && tree valid → BehaviorTree::tick()
//
// Tiered tick rates (VBfA RE: 5/10/20 frame modulo pattern):
//   TIER_NEAR  (< TIER_MED_M)  : every tick (full update)
//   TIER_MED   (< TIER_FAR_M)  : every 10 ticks (~1 s)
//   TIER_FAR   (>= TIER_FAR_M) : every 20 ticks (~2 s)
//   DORMANT (motivation==Dormant): every 20 ticks, minimal update only
//
// Thread safety: single-threaded. All entities ticked sequentially on the
// main logic thread. Do NOT call from render thread.
//
// Integration:
//   BTSystem bt_sys;
//   // In logic tick:
//   bt_sys.Tick(ctx, registry, nowMs);

static constexpr float BT_TIER_NEAR_M = 30.f;   // full tick within 30 m
static constexpr float BT_TIER_MED_M  = 80.f;   // reduced tick up to 80 m
// beyond 80 m → every 20 ticks; Dormant → every 20 ticks regardless

class BTSystem {
public:
    // nowMs: current game time in milliseconds (for TimerStart/TimerCheck nodes).
    // ctx:   engine context (frame_index for WeightedSelector RNG, delta_time, etc.)
    // reg:   EnTT registry — views (AgentState + BehaviorTreeComponent).
    void Tick(md::EngineContext& ctx, entt::registry& reg, uint32_t nowMs);

    uint32_t frame_idx() const noexcept { return frame_idx_; }

    // Called on entity removal to free owning trees.
    // Must be connected to entt::registry::on_destroy<BehaviorTreeComponent>().
    static void OnComponentDestroy(entt::registry& reg, entt::entity e);

    // Convenience: connect destroy listener to a registry.
    static void ConnectRegistry(entt::registry& reg) {
        reg.on_destroy<BehaviorTreeComponent>().connect<&BTSystem::OnComponentDestroy>();
    }

private:
    uint32_t frame_idx_ = 0;  // incremented each Tick(); used for tiered modulo skip
};
