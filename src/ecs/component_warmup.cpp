#include <monkey_dust/ecs/component_warmup.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/ecs/md_registry.h>

#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/ai_agent.h>
#include <monkey_dust/components/ai_script.h>
#include <monkey_dust/components/animator.h>
#include <monkey_dust/components/bleed_component.h>
#include <monkey_dust/components/bounty_component.h>
#include <monkey_dust/components/bt_component.h>
#include <monkey_dust/components/bt_components.h>
#include <monkey_dust/components/building.h>
#include <monkey_dust/components/char_body_state.h>
#include <monkey_dust/components/combat.h>
#include <monkey_dust/components/equipment.h>
#include <monkey_dust/components/faction.h>
#include <monkey_dust/components/flare_actor.h>
#include <monkey_dust/components/flare_sprite_anim.h>
#include <monkey_dust/components/health.h>
#include <monkey_dust/components/hierarchy.h>
#include <monkey_dust/components/inventory.h>
#include <monkey_dust/components/lock_component.h>
#include <monkey_dust/components/lua_script_component.h>
#include <monkey_dust/components/nav_agent.h>
#include <monkey_dust/components/npc_memory.h>
#include <monkey_dust/components/npc_needs.h>
#include <monkey_dust/components/npc_relationship.h>
#include <monkey_dust/components/player_controller.h>
#include <monkey_dust/components/prisoner_component.h>
#include <monkey_dust/components/projectile.h>
#include <monkey_dust/components/renderable.h>
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/components/skill_xp_accum.h>
#include <monkey_dust/components/stat_sheet.h>
#include <monkey_dust/components/stealth_component.h>
#include <monkey_dust/components/weapon_component.h>

#include <monkey_dust/combat/damage_calc.h>
#include <monkey_dust/combat/impact_event.h>
#include <monkey_dust/combat/limb_severance.h>

#include <monkey_dust/ai/npc_development.h>
#include <monkey_dust/ai/patrol_route.h>
#include <monkey_dust/ai/squad_controller.h>
#include <monkey_dust/ai/squad_signal.h>
#include <monkey_dust/ai/suspicious_item_group.h>

#include <monkey_dust/physics/jolt_world.h>
#include <monkey_dust/physics/ragdoll.h>

#include <monkey_dust/scripting/flow_graph.h>

#include <monkey_dust/world/alliance.h>
#include <monkey_dust/world/interior_portal.h>
#include <monkey_dust/world/shop_inventory.h>
#include <monkey_dust/world/world_transform.h>

#include <monkey_dust/building/building_integrity.h>

#include <cstdio>

namespace md {

void WarmUpEngineComponents() {
    auto& w = Registry::Get();

    w.component<AgentBlackboard>();
    w.component<AgentState>();
    w.component<AIAgent>();
    w.component<AIScript>();
    w.component<AnimatorComponent>();
    w.component<BehaviorTreeComponent>();
    w.component<BleedComponent>();
    w.component<BodyBaseline>();
    w.component<BountyComponent>();
    w.component<BTComponent>();
    w.component<Building>();
    w.component<CharBodyState>();
    w.component<Combat>();
    w.component<CombatModifiers>();
    w.component<DetachedLimb>();
    w.component<DirectorHintComponent>();
    w.component<EquipmentComponent>();
    w.component<Faction>();
    // Fear/Trust (npc_relationship.h) and FriendlyWith/HostileWith
    // (alliance.h) are private nested relation-tag types inside their
    // owning classes — can't be touched from here. If either ever gets
    // used for the first time from inside a JobGraph batch, that class
    // needs its own warm-up method (e.g. AllianceMatrix::WarmUp()) called
    // from here instead.
    w.component<FlareActorComponent>();
    w.component<FlareSpriteAnim>();
    w.component<FlowGraph>();
    w.component<Health>();
    w.component<ImpactEvent>();
    w.component<InteriorPortal>();
    w.component<Inventory>();
    w.component<LimbHealth>();
    w.component<LockComponent>();
    w.component<LuaScriptComponent>();
    w.component<MdManagedTag>();
    w.component<NavAgent>();
    w.component<NpcDevelopmentComponent>();
    w.component<NpcMemoryComponent>();
    w.component<NpcNeeds>();
    w.component<NpcRelationshipComponent>();
    w.component<ParentRef>();
    w.component<ChildrenRef>();
    w.component<PatrolRoute>();
    w.component<PhysicsAgent>();
    w.component<PlayerController>();
    w.component<PrisonerComponent>();
    w.component<ProjectileComponent>();
    w.component<RagdollComponent>();
    w.component<Renderable>();
    w.component<SenseComponent>();
    w.component<SenseModifiers>();
    w.component<ShopInventory>();
    w.component<SkillXpAccum>();
    w.component<SquadController>();
    w.component<SquadMemberComponent>();
    w.component<StatSheet>();
    w.component<StealthComponent>();
    w.component<SuspiciousItemGroupComponent>();
    w.component<WeaponComponent>();
    w.component<WorldTransform>();
    w.component<CollapseState>();

    fprintf(stdout, "[ComponentWarmup] all engine ECS component types registered\n");
}

}  // namespace md
