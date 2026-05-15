#include <monkey_dust/ai/bt_system.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/world/transform_soa.h>

// BT tick-rate LOD thresholds (squared metres).
// Near  < 20m  → always tick at 10 TPS
// Mid  20-60m  → tick every 2nd logic tick (5 TPS)
// Far   > 60m  → tick every 5th logic tick (2 TPS)
static constexpr float BT_LOD_NEAR_SQ = 20.0f * 20.0f;
static constexpr float BT_LOD_FAR_SQ  = 60.0f * 60.0f;

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

    // Phase 3: tick active behavior trees with distance-based LOD.
    // Frame_flags already cleared in phases 1+2 for ALL entities regardless of LOD.
    const auto& tsoa  = TransformSoA::Get();
    const uint32_t fi = ctx.frame_index;
    auto bt_view = reg.view<AgentState, BehaviorTreeComponent>();
    bt_view.each([&](entt::entity e, AgentState& as, BehaviorTreeComponent& btc) {
        if (!btc.enabled || !btc.tree || !btc.tree->isValid()) return;
        if (as.lcflags.test(lcf::IS_SUSPENDED)) return;  // Batch 11 P8: suspension gate

        // LOD: read dist_sq from TransformSoA if entity has a valid slot.
        const auto* wt = reg.try_get<WorldTransform>(e);
        if (wt && wt->slot < (uint32_t)tsoa.active_count) {
            float dsq = tsoa.dist_sq[wt->slot];
            if (dsq > BT_LOD_FAR_SQ) {
                // Far: distribute across 5 phases via entity id so load is even.
                if ((entt::to_integral(e) % 5u) != (fi % 5u)) return;
            } else if (dsq > BT_LOD_NEAR_SQ) {
                // Mid: every other tick.
                if ((entt::to_integral(e) % 2u) != (fi % 2u)) return;
            }
        }

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
