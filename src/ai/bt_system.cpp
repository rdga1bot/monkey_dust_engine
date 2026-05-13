#include <monkey_dust/ai/bt_system.h>

void BTSystem::Tick(md::EngineContext& ctx, entt::registry& reg, uint32_t nowMs) {
    // Phase 1: clear frame_flags + expire stale DirectorHints
    auto hint_view = reg.view<AgentState, DirectorHintComponent>();
    hint_view.each([&](entt::entity, AgentState& as, DirectorHintComponent& hint) {
        // C13: clear per-frame signals before BT tick
        as.frame_flags = 0;
        // Stale hint expiry — prevents Director hints from blocking BT indefinitely
        if (hint.role_pending) {
            if (++hint.pending_ticks > DirectorHintComponent::MAX_PENDING_TICKS) {
                hint.role_pending  = false;
                hint.pending_ticks = 0;
            }
        }
    });

    // Phase 2: clear frame_flags for entities with no DirectorHintComponent
    auto bare_view = reg.view<AgentState>(entt::exclude<DirectorHintComponent>);
    bare_view.each([](entt::entity, AgentState& as) {
        as.frame_flags = 0;
    });

    // Phase 3: tick all active behavior trees
    auto bt_view = reg.view<AgentState, BehaviorTreeComponent>();
    bt_view.each([&](entt::entity e, AgentState&, BehaviorTreeComponent& btc) {
        if (!btc.enabled || !btc.tree || !btc.tree->isValid()) return;
        btc.tree->tick(ctx, e, nowMs);
    });
}

void BTSystem::OnComponentDestroy(entt::registry& reg, entt::entity e) {
    auto* btc = reg.try_get<BehaviorTreeComponent>(e);
    if (btc && btc->owning && btc->tree) {
        delete btc->tree;
        btc->tree = nullptr;
    }
}
