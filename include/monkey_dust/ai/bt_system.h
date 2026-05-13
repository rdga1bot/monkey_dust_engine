#pragma once
#include <monkey_dust/ecs/engine_context.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/bt_components.h>
#include <entt/entt.hpp>
#include <cstdint>

// ── BTSystem ──────────────────────────────────────────────────────────────────
// Main BT execution loop (CATHODE BehaviorManager analog).
// Call Tick() once per logic tick (10 TPS).
//
// Per-entity sequence:
//   1. Clear AgentState::frame_flags (C13 invariant — single-tick signals reset)
//   2. Tick DirectorHintComponent: expire stale hints (>MAX_PENDING_TICKS)
//   3. If BehaviorTreeComponent::enabled && tree valid → BehaviorTree::tick()
//
// Thread safety: single-threaded. All entities ticked sequentially on the
// main logic thread. Do NOT call from render thread.
//
// Integration:
//   BTSystem bt_sys;
//   // In logic tick:
//   bt_sys.Tick(ctx, registry, nowMs);
class BTSystem {
public:
    // nowMs: current game time in milliseconds (for TimerStart/TimerCheck nodes).
    // ctx:   engine context (frame_index for WeightedSelector RNG, delta_time, etc.)
    // reg:   EnTT registry — views (AgentState + BehaviorTreeComponent).
    void Tick(md::EngineContext& ctx, entt::registry& reg, uint32_t nowMs);

    // Called on entity removal to free owning trees.
    // Must be connected to entt::registry::on_destroy<BehaviorTreeComponent>().
    static void OnComponentDestroy(entt::registry& reg, entt::entity e);

    // Convenience: connect destroy listener to a registry.
    static void ConnectRegistry(entt::registry& reg) {
        reg.on_destroy<BehaviorTreeComponent>().connect<&BTSystem::OnComponentDestroy>();
    }
};
