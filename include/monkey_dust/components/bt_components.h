#pragma once
#include <monkey_dust/ai/behavior_tree.h>
#include <monkey_dust/ai/role_registry.h>
#include <cstdint>

// ── BehaviorTreeComponent ─────────────────────────────────────────────────────
// Per-entity BT ownership ECS component.
//
// Ownership modes:
//   owning=true  — this entity owns the BehaviorTree instance.
//                  BTSystem calls delete tree when entity is destroyed.
//   owning=false — shared template (loaded once, pointed to by many entities).
//                  Caller manages lifetime (e.g. via BTNodePool).
//
// Use the shared-template pattern for NPCs of the same type:
//   BehaviorTree* patrol_tmpl = pool.AllocTree(); // build once
//   ...
//   for (auto npc : spawn_npc_enemies) {
//       reg.emplace<BehaviorTreeComponent>(npc, patrol_tmpl, false, true);
//   }
//
// NOTE: with shared templates, BT::tick() mutates m_state[] which is INSIDE
// the BehaviorTree object. For safe shared templates, use a per-entity state
// copy (not yet implemented — safe for now since NPCs tick sequentially
// in BTSystem::Tick on the main logic thread).
struct BehaviorTreeComponent {
    BehaviorTree* tree    = nullptr;  // BT template or per-entity owned tree
    bool          owning  = false;    // if true: BTSystem destructs on entity remove
    bool          enabled = true;     // when false: BTSystem skips this entity
    uint8_t       _pad[6];

    BehaviorTreeComponent() = default;
    explicit BehaviorTreeComponent(BehaviorTree* t, bool owns = false, bool en = true) noexcept
        : tree(t), owning(owns), enabled(en) {}

    // Convenience: allocate a fresh owned tree
    static BehaviorTreeComponent MakeOwned() {
        BehaviorTreeComponent c;
        c.tree    = new BehaviorTree();
        c.owning  = true;
        c.enabled = true;
        return c;
    }
};

// ── DirectorHintComponent ─────────────────────────────────────────────────────
// CATHODE ActionPerformRole + ConditionIsPerformingRoleOrCouldPerformRole analog.
// DirectorSystem writes this; BT RoleClaim nodes consume and clear it.
//
// Flow:
//   1. DirectorSystem::Tick() sets suggested_role + query_id + role_pending=true
//      for eligible NPC entities.
//   2. BT RoleClaim node checks: if role_pending && role == suggested_role,
//      calls RoleRegistry::claim(role, query_id, entity).
//      On success: clears role_pending (hint consumed).
//      On failure: hint remains pending for next tick.
//   3. BTSystem clears stale hints (role_pending=true for > MAX_PENDING_TICKS).
//
// NOTE: DirectorHintComponent is intentionally separate from AgentState to
// keep AI policy (Director) decoupled from BT execution state.
struct DirectorHintComponent {
    static constexpr uint8_t MAX_PENDING_TICKS = 10; // stale-hint TTL

    NpcRole  suggested_role  = NpcRole::None;
    uint32_t query_id        = 0;
    bool     role_pending    = false;  // true = hint not yet consumed by BT
    uint8_t  pending_ticks   = 0;      // incremented each tick while role_pending
    uint8_t  _pad[2];
};
static_assert(sizeof(DirectorHintComponent) == 12, "DirectorHintComponent size");
