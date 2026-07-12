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

flecs::query<AIAgent, BTComponent, WorldTransform>& AIAgentBTWorldTransform() {
    static auto q = MdRegistry::Get().Raw().query<AIAgent, BTComponent, WorldTransform>();
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
