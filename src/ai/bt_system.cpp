#include <monkey_dust/ai/bt_system.h>
#include <monkey_dust/ai/ai_budget.h>
#include <monkey_dust/components/ai_agent.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/world/transform_soa.h>

// VBfA-AI1: 3-tier BT LOD (Viking Battle for Asgard 2008 AI architecture).
// Source: tasks.bin AI_CHARACTER_LOCO[16] + character_build_pass[u] analysis.
//
// Tier 1  dist < 30m  → full 10 TPS tick every frame          (100ms)
// Tier 2  30–80m      → tick every 4th frame, entity-id spread (400ms ≈ 2.5 TPS)
// Tier 3  dist > 80m  → SKIP entirely; only CrowdSystem/NavAgent locomotion runs
//
// Entity-id spreading (% N) distributes Tier2 load evenly across frames instead
// of clustering all mid-range entities in the same tick (better than an accumulator).
// Tier3 complete skip is the critical VBfA insight: 500 NPC scenario → only ~50
// entities in Tier1, ~100 in Tier2 (25/tick) — rest = zero BT cost.
static constexpr float BT_LOD_TIER1_SQ = 30.0f * 30.0f;   //  900 m²
static constexpr float BT_LOD_TIER2_SQ = 80.0f * 80.0f;   // 6400 m²

void BTSystem::Tick(md::EngineContext& ctx, entt::registry& reg, uint32_t nowMs) {
    ++frame_idx_;

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
    // VBfA RE §8: tiered modulo rates — 5f / 10f / 20f (logarithmic load scaling).
    // CATHODE RE §7: Dormant state = NPC_Sleeping_Android, minimal update tier.
    const auto& tsoa = TransformSoA::Get();
    const uint32_t fi = frame_idx_;
    auto bt_view = reg.view<AgentState, BehaviorTreeComponent>();
    bt_view.each([&](entt::entity e, AgentState& as, BehaviorTreeComponent& btc) {
        if (!btc.enabled || !btc.tree || !btc.tree->isValid()) return;
        if (as.lcflags.test(lcf::IS_SUSPENDED)) return;  // Batch 11 P8: suspension gate

        // VBfA-AI1: 3-tier LOD via TransformSoA::dist_sq (camera/player distance).
        const auto* wt = reg.try_get<WorldTransform>(e);
        if (wt && wt->slot < (uint32_t)tsoa.active_count) {
            float dsq = tsoa.dist_sq[wt->slot];
            if (dsq > BT_LOD_TIER2_SQ) {
                // Tier 3 (>80m) OR Dormant: every 20 ticks (~2 s at 10 TPS).
                // VBfA §8: modulo 20 (not complete skip) — distant NPCs still wake if
                // player approaches. CATHODE NPC_Sleeping_Android uses same rate.
                if (fi % 20u != entt::to_integral(e) % 20u) return;
            } else if (dsq > BT_LOD_TIER1_SQ) {
                // Tier 2 (30–80m): every 10 ticks (~1 s) with entity-id spread.
                if (fi % 10u != entt::to_integral(e) % 10u) return;
            }
            // Tier 1 (<30m): always tick — falls through here.
        }
        // CATHODE RE: Dormant motivation → far-tier modulo regardless of distance
        if (as.motivation == MotivationType::Dormant) {
            if (fi % 20u != entt::to_integral(e) % 20u) return;
        }

        // VBfA-AI3: budget check (after LOD, before expensive BT tick).
        // Player is exempt; schedule NPCs (bt_template_id==255) cost 0.2;
        // all other NPCs cost 1.0 (default combat unit).
        float cost = BtBudgetCost::kGuard;
        if (as.lcflags.test(lcf::IS_PLAYER)) {
            cost = BtBudgetCost::kPlayer;
        } else {
            const AIAgent* ag = reg.try_get<AIAgent>(e);
            if (ag && ag->bt_template_id == 255)
                cost = BtBudgetCost::kScheduleNpc;
        }
        if (!AIBudget::Get().TryConsume(cost)) return;  // budget exhausted: skip BT

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
