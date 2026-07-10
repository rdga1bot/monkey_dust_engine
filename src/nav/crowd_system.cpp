// CrowdSystem — DetourCrowd ORCA steering.
#include <monkey_dust/nav/crowd_system.h>
#include <monkey_dust/components/nav_agent.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/world/terrain_query.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <cstdio>
#include <cstring>
#include <cmath>

void CrowdSystem::Init(const dtNavMesh* nav_mesh, int max_agents) {
    Shutdown();
    if (!nav_mesh) { fprintf(stderr, "[Crowd] null navmesh\n"); return; }
    nav_mesh_ = nav_mesh;
    crowd_ = dtAllocCrowd();
    if (!crowd_) { fprintf(stderr, "[Crowd] dtAllocCrowd failed\n"); return; }
    if (!crowd_->init(max_agents, 0.6f, const_cast<dtNavMesh*>(nav_mesh))) {
        fprintf(stderr, "[Crowd] crowd->init failed\n");
        dtFreeCrowd(crowd_); crowd_ = nullptr; return;
    }
    for (int i = 0; i < MAX_AGENTS; ++i) agent_entity_[i] = entt::null;
    fprintf(stdout, "[Crowd] Init: max_agents=%d\n", max_agents);
}

void CrowdSystem::Shutdown() {
    if (crowd_) { dtFreeCrowd(crowd_); crowd_ = nullptr; }
    nav_mesh_ = nullptr;
}

int CrowdSystem::AddAgent(entt::entity e, float x, float z,
                           float radius, float max_speed) {
    if (!crowd_) return -1;
    dtCrowdAgentParams ap = {};
    ap.radius             = radius;
    ap.height             = 1.8f;
    ap.maxAcceleration    = 8.0f;
    ap.maxSpeed           = max_speed;
    ap.collisionQueryRange= radius * 12.0f;
    ap.pathOptimizationRange = radius * 30.0f;
    ap.separationWeight   = 2.0f;
    ap.updateFlags        = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OBSTACLE_AVOIDANCE |
                            DT_CROWD_SEPARATION;
    ap.obstacleAvoidanceType = 3;

    float pos[3] = { x, 0.f, z };
    int idx = crowd_->addAgent(pos, &ap);
    if (idx >= 0 && idx < MAX_AGENTS) agent_entity_[idx] = e;
    return idx;
}

void CrowdSystem::RemoveAgent(int idx) {
    if (!crowd_ || idx < 0) return;
    crowd_->removeAgent(idx);
    if (idx < MAX_AGENTS) agent_entity_[idx] = entt::null;
}

void CrowdSystem::SetTarget(int idx, float tx, float tz) {
    if (!crowd_ || idx < 0) return;
    const dtNavMeshQuery* q = crowd_->getNavMeshQuery();
    if (!q) return;
    dtQueryFilter filter;
    float ext[3] = { 2.f, 4.f, 2.f };
    float target[3] = { tx, 0.f, tz };
    float nearest[3];
    dtPolyRef ref = 0;
    q->findNearestPoly(target, ext, &filter, &ref, nearest);
    if (ref) crowd_->requestMoveTarget(idx, ref, nearest);
}

void CrowdSystem::Update(float dt) {
    if (crowd_) crowd_->update(dt, nullptr);
}

void CrowdSystem::FlushRange(MdRegistry& reg, int start, int end) const {
    if (!crowd_) return;
    if (end > MAX_AGENTS) end = MAX_AGENTS;
    for (int i = start; i < end; ++i) {
        if (agent_entity_[i] == entt::null) continue;
        if (!reg.Valid(agent_entity_[i])) continue;
        const dtCrowdAgent* ag = crowd_->getAgent(i);
        if (!ag || !ag->active) continue;

        if (reg.AllOf<WorldTransform>(agent_entity_[i])) {
            auto& tr = reg.Get<WorldTransform>(agent_entity_[i]);
            tr.x = ag->npos[0];
            tr.z = ag->npos[2];
            // Keep Y on terrain surface as agent moves (DetourCrowd npos[1] may be
            // stale/zero when navmesh is flat; TerrainQuery is always authoritative).
            tr.y = TerrainQuery::Get().GetHeight(tr.x, tr.z);
        }
        if (reg.AllOf<NavAgent>(agent_entity_[i])) {
            auto& nav = reg.Get<NavAgent>(agent_entity_[i]);
            float vx = ag->vel[0], vz = ag->vel[2];
            float speed = sqrtf(vx*vx + vz*vz);
            if (speed > 0.2f) {
                nav.is_moving  = true;
                nav.move_speed = speed / 3.5f;
                // Sync actual crowd velocity so render extrapolation matches real movement.
                // BT overwrites desired_vel at the next tick before Jolt uses it.
                nav.desired_vel_x = vx;
                nav.desired_vel_z = vz;
            } else {
                nav.is_moving     = false;
                nav.move_speed    = 0.f;
                nav.desired_vel_x = 0.f;
                nav.desired_vel_z = 0.f;
            }
        }
    }
}

void CrowdSystem::FlushToNavAgent(MdRegistry& reg) const {
    FlushRange(reg, 0, MAX_AGENTS);
}

void CrowdSystem::GetPosition(int idx, float& x, float& z) const {
    if (!crowd_ || idx < 0) { x = z = 0.f; return; }
    const dtCrowdAgent* ag = crowd_->getAgent(idx);
    if (!ag) { x = z = 0.f; return; }
    x = ag->npos[0]; z = ag->npos[2];
}
