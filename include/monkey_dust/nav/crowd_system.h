#pragma once
// CrowdSystem — DetourCrowd wrapper for ORCA steering.
// DetourCrowd is already compiled into engine (see engine/CMakeLists.txt).
// Usage:
//   CrowdSystem::Get().Init(NavSystem::Get().GetRawMesh());
//   int idx = CrowdSystem::Get().AddAgent(entity, x, z);
//   // Per logic tick:
//   CrowdSystem::Get().SetTarget(idx, tx, tz);
//   CrowdSystem::Get().Update(dt);
//   CrowdSystem::Get().FlushToNavAgent(registry);

#include <entt/entt.hpp>

class dtCrowd;
struct dtNavMesh;
struct dtCrowdAgentParams;

class CrowdSystem {
public:
    static CrowdSystem& Get() { static CrowdSystem s; return s; }
    static constexpr int MAX_AGENTS = 128;

    void Init(const dtNavMesh* nav_mesh, int max_agents = MAX_AGENTS);
    void Shutdown();
    bool IsReady() const { return crowd_ != nullptr; }

    // Register an NPC entity. Returns agent index (0..MAX_AGENTS-1) or -1.
    int  AddAgent(entt::entity e, float x, float z,
                  float radius = 0.35f, float max_speed = 3.5f);
    void RemoveAgent(int idx);

    // Set a new target world position for agent idx.
    void SetTarget(int idx, float tx, float tz);

    // Advance simulation by dt seconds (call once per logic tick).
    void Update(float dt);

    // Write crowd agent velocities into NavAgent::desired_vel / is_moving.
    // Call after Update(), before JoltWorld::Step().
    void FlushToNavAgent(entt::registry& reg) const;

    // Shortcut: position of agent idx (x, z).
    void GetPosition(int idx, float& x, float& z) const;

private:
    CrowdSystem() = default;
    dtCrowd* crowd_ = nullptr;
    entt::entity agent_entity_[MAX_AGENTS] = {};
    const dtNavMesh* nav_mesh_ = nullptr;
};
