#pragma once
// building_integrity.h — Structural support graph (Phase 4, Task 4.7b)
// Kenshi-style: every placed piece needs ground support. Unsupported → collapses.
// BFS propagation on place/destroy. Max nodes = MAX_BUILDINGS (8192).
//
// Adjacency: each node checks ±X, ±Y, ±Z neighbours (6-connected grid).
// Support: a node is supported if it has a FOUNDATION neighbour OR a
//          neighbour that is itself supported (transitive, BFS from foundations).
//
// Integration:
//   BuildSystem::Place()   → IntegrityGraph::OnPlace(e, gx, gz)
//   BuildSystem::Demolish()→ IntegrityGraph::OnDestroy(e)
//   Both call PropagateSupport() which marks collapsed nodes.
//
// Collapsed nodes have component CollapseState added in the ECS registry;
//   the render system spawns debris at their WorldTransform position.

#include <monkey_dust/ecs/registry.h>
#include <cstdint>
#include <cstring>

static constexpr int INTEGRITY_MAX_NODES = 8192;
static constexpr int INTEGRITY_BFS_STACK = 512;

struct CollapseState {
    float timer_s = 0.f;   // counts up; render system uses for collapse animation
};

struct BuildingNode {
    entt::entity entity       = entt::null;
    int16_t      gx           = 0;
    int16_t      gy           = 0;   // vertical layer (0 = ground)
    int16_t      gz           = 0;
    bool         has_foundation = false;  // placed on ground tile
    bool         is_supported   = false;
    bool         is_collapsing  = false;
    bool         active         = false;
};

class IntegrityGraph {
public:
    static IntegrityGraph& Get() { static IntegrityGraph s; return s; }

    // Register a newly placed building piece.
    void OnPlace(entt::entity e, int gx, int gy, int gz, bool is_foundation) {
        if (count_ >= INTEGRITY_MAX_NODES) return;
        BuildingNode& n = nodes_[count_++];
        n.entity        = e;
        n.gx            = (int16_t)gx;
        n.gy            = (int16_t)gy;
        n.gz            = (int16_t)gz;
        n.has_foundation= is_foundation;
        n.active        = true;
        PropagateSupport();
    }

    // Remove a building piece (on demolish or collapse).
    void OnDestroy(entt::entity e, entt::registry& reg) {
        int idx = FindIdx(e);
        if (idx < 0) return;
        // Swap-erase
        nodes_[idx] = nodes_[--count_];
        PropagateSupport();
        // Any node now marked !is_supported → trigger collapse.
        for (int i = 0; i < count_; ++i) {
            BuildingNode& n = nodes_[i];
            if (!n.is_supported && !n.is_collapsing) {
                n.is_collapsing = true;
                (void)reg.get_or_emplace<CollapseState>(n.entity);
            }
        }
    }

    // Remove all nodes in a AABB (chunk unload).
    void PurgeChunk(int gx_min, int gz_min, int gx_max, int gz_max) {
        for (int i = count_ - 1; i >= 0; --i) {
            const BuildingNode& n = nodes_[i];
            if (n.gx >= gx_min && n.gx <= gx_max &&
                n.gz >= gz_min && n.gz <= gz_max) {
                nodes_[i] = nodes_[--count_];
            }
        }
    }

    int Count() const { return count_; }

private:
    IntegrityGraph() { memset(nodes_, 0, sizeof(nodes_)); }

    int FindIdx(entt::entity e) const {
        for (int i = 0; i < count_; ++i)
            if (nodes_[i].entity == e) return i;
        return -1;
    }

    // BFS from all foundation nodes to mark reachability.
    void PropagateSupport() {
        // Reset support flags.
        for (int i = 0; i < count_; ++i)
            nodes_[i].is_supported = nodes_[i].has_foundation;

        // BFS stack (indices into nodes_).
        int stack[INTEGRITY_BFS_STACK];
        int top = 0;

        // Seed with foundations.
        for (int i = 0; i < count_; ++i)
            if (nodes_[i].has_foundation && top < INTEGRITY_BFS_STACK)
                stack[top++] = i;

        while (top > 0) {
            int cur = stack[--top];
            const BuildingNode& cn = nodes_[cur];
            // Check 6 neighbours.
            static const int dx[6] = { 1,-1, 0, 0, 0, 0 };
            static const int dy[6] = { 0, 0, 1,-1, 0, 0 };
            static const int dz[6] = { 0, 0, 0, 0, 1,-1 };
            for (int d = 0; d < 6; ++d) {
                int nx = cn.gx + dx[d];
                int ny = cn.gy + dy[d];
                int nz = cn.gz + dz[d];
                for (int j = 0; j < count_; ++j) {
                    BuildingNode& nb = nodes_[j];
                    if (!nb.active || nb.is_supported) continue;
                    if (nb.gx == nx && nb.gy == ny && nb.gz == nz) {
                        nb.is_supported = true;
                        if (top < INTEGRITY_BFS_STACK)
                            stack[top++] = j;
                    }
                }
            }
        }
    }

    BuildingNode nodes_[INTEGRITY_MAX_NODES];
    int          count_ = 0;
};
