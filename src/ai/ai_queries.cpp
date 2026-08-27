#include <monkey_dust/ai/ai_queries.h>
#include <monkey_dust/ai/squad_controller.h>

namespace ai_queries {

flecs::query<NpcNeeds>& NpcNeedsOnly() {
    static auto q = MdRegistry::Get().Raw().query<NpcNeeds>();
    return q;
}

flecs::query<BleedComponent, LimbHealth>& BleedLimbHealth() {
    static auto q = MdRegistry::Get().Raw().query<BleedComponent, LimbHealth>();
    return q;
}

flecs::query<InjuryState, LimbHealth>& InjuryLimbHealth() {
    static auto q = MdRegistry::Get().Raw().query<InjuryState, LimbHealth>();
    return q;
}

flecs::query<AgentState, NpcSchedule>& AgentStateSchedule() {
    static auto q = MdRegistry::Get().Raw().query<AgentState, NpcSchedule>();
    return q;
}

flecs::query<AIAgent, BTComponent>& AIAgentBT() {
    static auto q = MdRegistry::Get().Raw().query<AIAgent, BTComponent>();
    return q;
}

flecs::query<SquadController>& SquadControllers() {
    static auto q = MdRegistry::Get().Raw().query<SquadController>();
    return q;
}

// task #8 Phase 4: order_by<AIAgent> replaces the old EnTT-era
// MdRegistry::Sort<AIAgent>() (a periodic, timer-gated pool-wide physical
// reorder — a no-op ever since the B3.4 flecs swap, since flecs has no
// pool to physically reorder the way entt did). flecs's per-query
// order_by does the equivalent job properly instead: entities of the same
// faction_id cluster together in this query's iteration order (the actual
// benefit logic_tick.cpp's old comment described — faction cache-locality
// for BT/combat evaluation), and per flecs.h's own doc comment on
// order_by, re-sorting only happens "if that component has changed, or
// when the entity order in the table has changed" — i.e. automatically
// exactly when needed (an AIAgent spawned/despawned/its faction_id
// written), not on a blind 190-tick timer that could leave a freshly
// spawned NPC unsorted for up to ~19s or re-sort for nothing when
// nothing changed.
static int CompareAIAgentFaction(flecs::entity_t, const AIAgent* a,
                                  flecs::entity_t, const AIAgent* b) {
    return (a->faction_id > b->faction_id) - (a->faction_id < b->faction_id);
}

flecs::query<AIAgent, BTComponent, WorldTransform, AIAgentTickState>& AIAgentBTWorldTransform() {
    // AIAgentTickState is a 4th term alongside AIAgent, not folded into it
    // (see ai_agent.h's doc comment on the type) -- writing it every tick
    // must NOT dirty AIAgent's own change monitor, which order_by<AIAgent>
    // below depends on staying clean while JobGraph runs this batch in
    // multithreaded mode (audit S1-00, 2026-08-27).
    static auto q = MdRegistry::Get().Raw()
        .query_builder<AIAgent, BTComponent, WorldTransform, AIAgentTickState>()
        .order_by<AIAgent>(CompareAIAgentFaction)
        .build();
    return q;
}

flecs::query<AgentState, WorldTransform>& AgentStateWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<AgentState, WorldTransform>();
    return q;
}

flecs::query<AgentState>& AgentStateOnly() {
    static auto q = MdRegistry::Get().Raw().query<AgentState>();
    return q;
}

flecs::query<SenseComponent, WorldTransform, AgentState>& SenseWorldTransformAgentState() {
    static auto q = MdRegistry::Get().Raw().query<SenseComponent, WorldTransform, AgentState>();
    return q;
}

flecs::query<NoiseEmitter, WorldTransform>& NoiseWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<NoiseEmitter, WorldTransform>();
    return q;
}

flecs::query<SenseComponent, WorldTransform>& SenseWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<SenseComponent, WorldTransform>();
    return q;
}

flecs::query<SmellEmitter, WorldTransform>& SmellWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<SmellEmitter, WorldTransform>();
    return q;
}

flecs::query<AgentState, AgentBlackboard, Combat, WorldTransform>& AgentFullCombat() {
    static auto q = MdRegistry::Get().Raw().query<AgentState, AgentBlackboard, Combat, WorldTransform>();
    return q;
}

flecs::query<Inventory, WorldTransform>& InventoryWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<Inventory, WorldTransform>();
    return q;
}

flecs::query<Building, WorldTransform>& BuildingWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<Building, WorldTransform>();
    return q;
}

flecs::query<StatSheet, WorldTransform>& StatSheetWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<StatSheet, WorldTransform>();
    return q;
}

flecs::query<ShopInventory, WorldTransform>& ShopInventoryWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<ShopInventory, WorldTransform>();
    return q;
}

void WarmAll() {
    (void)NpcNeedsOnly();
    (void)BleedLimbHealth();
    (void)InjuryLimbHealth();
    (void)AgentStateSchedule();
    (void)AIAgentBT();
    (void)SquadControllers();
    (void)AIAgentBTWorldTransform();
    (void)AgentStateWorldTransform();
    (void)AgentStateOnly();
    (void)SenseWorldTransformAgentState();
    (void)NoiseWorldTransform();
    (void)SenseWorldTransform();
    (void)SmellWorldTransform();
    (void)AgentFullCombat();
    (void)InventoryWorldTransform();
    (void)BuildingWorldTransform();
    (void)StatSheetWorldTransform();
    (void)ShopInventoryWorldTransform();
}

} // namespace ai_queries
