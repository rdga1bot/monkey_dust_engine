#pragma once
#include "pcg_node.h"
#include <monkey_dust/platform/md_log.h>

namespace md {

// ── Static memory arenas (BSS, no heap) ──────────────────────────────────────
// Each node slot has its own heightmap, mask, scatter, and biome arrays.
// PcgGraph lends the appropriate pointer to the node before Execute().
namespace pcg_arena {
    static float        height [32][PCG_FIELD_RES * PCG_FIELD_RES];
    static float        mask   [32][PCG_FIELD_RES * PCG_FIELD_RES];
    static ScatterPoint scatter[32][PCG_MAX_SCATTER];
    static uint8_t      biome  [32][PCG_FIELD_RES * PCG_FIELD_RES];
}

// ── PcgLink — typed connection between two pins ───────────────────────────────
struct PcgLink {
    uint8_t from_node = 0, from_pin = 0;
    uint8_t to_node   = 0, to_pin   = 0;
    bool    alive     = false;
};

// ── PcgGraph — DAG execution engine ──────────────────────────────────────────
struct PcgGraph {
    static constexpr int MAX_NODES = 32;
    static constexpr int MAX_LINKS = 64;

    PcgNode*  nodes[MAX_NODES] = {};   // non-owning; concrete nodes live in panel
    int       node_count       = 0;
    PcgLink   links[MAX_LINKS] = {};
    int       link_count       = 0;

    int       topo[MAX_NODES]  = {};   // topological order indices
    int       topo_count       = 0;
    bool      topo_valid       = false;

    uint32_t  exec_version     = 0;    // bumped after each Execute()
    bool      needs_rebuild    = true; // set true when any param changes

    // ── Public API ────────────────────────────────────────────────────────

    // Adds a node pointer (caller owns memory).
    // Returns slot index, -1 on overflow.
    int AddNode(PcgNode* n) {
        if (node_count >= MAX_NODES) {
            MD_LOG(MD_LOG_WARNING, "[PcgGraph] MAX_NODES=%d reached", MAX_NODES);
            return -1;
        }
        int idx = node_count++;
        nodes[idx] = n;
        n->id      = (uint16_t)(idx + 1); // 1-based id stored inside node
        topo_valid = false;
        return idx;
    }

    void RemoveNode(int idx) {
        if (idx < 0 || idx >= node_count || !nodes[idx]) return;
        nodes[idx]->alive = false;
        nodes[idx]       = nullptr;
        topo_valid       = false;
        needs_rebuild    = true;
    }

    // from_node/to_node are slot indices (0-based).
    bool AddLink(int from_node, int from_pin, int to_node, int to_pin) {
        if (link_count >= MAX_LINKS) {
            MD_LOG(MD_LOG_WARNING, "[PcgGraph] MAX_LINKS=%d reached", MAX_LINKS);
            return false;
        }
        // Type check
        if (nodes[from_node] && nodes[to_node]) {
            PcgDataType from_t = nodes[from_node]->OutType();
            PcgDataType to_t   = nodes[to_node]->InType(to_pin);
            if (!PcgPinCompatible(from_t, to_t)) {
                MD_LOG(MD_LOG_WARNING, "[PcgGraph] incompatible pin types %d→%d", (int)from_t, (int)to_t);
                return false;
            }
        }
        PcgLink& l = links[link_count++];
        l = { (uint8_t)from_node, (uint8_t)from_pin,
               (uint8_t)to_node,  (uint8_t)to_pin, true };
        topo_valid    = false;
        needs_rebuild = true;
        MarkDirty(to_node);
        return true;
    }

    void RemoveLink(int link_idx) {
        if (link_idx < 0 || link_idx >= link_count) return;
        int to = links[link_idx].to_node;
        links[link_idx].alive = false;
        topo_valid    = false;
        needs_rebuild = true;
        MarkDirty(to);
    }

    // Mark node and all downstream nodes dirty.
    void MarkDirty(int node_idx) {
        if (node_idx < 0 || node_idx >= node_count || !nodes[node_idx]) return;
        if (!nodes[node_idx]->alive) return;
        nodes[node_idx]->MarkDirty();
        needs_rebuild = true;
        // Propagate downstream
        for (int i = 0; i < link_count; ++i) {
            if (!links[i].alive) continue;
            if (links[i].from_node == (uint8_t)node_idx)
                MarkDirty(links[i].to_node);
        }
    }

    // Kahn's algorithm — fills topo[]; returns false if cycle detected.
    bool TopologicalSort() {
        if (topo_valid) return true;

        uint8_t in_degree[MAX_NODES] = {};
        for (int i = 0; i < link_count; ++i) {
            if (!links[i].alive) continue;
            in_degree[links[i].to_node]++;
        }

        // Queue of zero-in-degree nodes
        int queue[MAX_NODES];
        int qhead = 0, qtail = 0;
        for (int i = 0; i < node_count; ++i)
            if (nodes[i] && nodes[i]->alive && in_degree[i] == 0)
                queue[qtail++] = i;

        topo_count = 0;
        while (qhead < qtail) {
            int cur = queue[qhead++];
            topo[topo_count++] = cur;
            for (int i = 0; i < link_count; ++i) {
                if (!links[i].alive) continue;
                if (links[i].from_node != (uint8_t)cur) continue;
                int dst = links[i].to_node;
                if (--in_degree[dst] == 0)
                    queue[qtail++] = dst;
            }
        }

        // Cycle check
        int alive = 0;
        for (int i = 0; i < node_count; ++i)
            if (nodes[i] && nodes[i]->alive) ++alive;
        if (topo_count < alive) {
            MD_LOG(MD_LOG_WARNING, "[PcgGraph] cycle detected in node graph");
            return false;
        }

        topo_valid = true;
        return true;
    }

    // Execute all dirty nodes in topological order.
    void Execute() {
        if (!TopologicalSort()) return;

        // Gather inputs for each node using the static cached output of upstream nodes.
        for (int ti = 0; ti < topo_count; ++ti) {
            int idx = topo[ti];
            PcgNode* n = nodes[idx];
            if (!n || !n->alive) continue;

            // Assign arena pointers before Execute so node can write output:
            SetupArenaPointers(idx, n->cached);

            // Collect inputs from upstream nodes
            PcgData inputs[PCG_MAX_INPUTS] = {};
            int     in_count = 0;
            for (int li = 0; li < link_count && in_count < PCG_MAX_INPUTS; ++li) {
                if (!links[li].alive) continue;
                if (links[li].to_node != (uint8_t)idx) continue;
                int pin   = links[li].to_pin;
                int src   = links[li].from_node;
                if (src >= 0 && src < node_count && nodes[src] && nodes[src]->alive) {
                    inputs[pin] = nodes[src]->cached;
                    if (pin >= in_count) in_count = pin + 1;
                }
            }

            n->GetOutput(inputs, in_count);
        }

        ++exec_version;
        needs_rebuild = false;
    }

    // Find slot index for a given editor-facing ID (1-based node id).
    int FindByEditorId(int editor_id) const {
        for (int i = 0; i < node_count; ++i)
            if (nodes[i] && nodes[i]->id == (uint16_t)editor_id)
                return i;
        return -1;
    }

private:
    void SetupArenaPointers(int idx, PcgData& cached_ref) {
        if (idx < 0 || idx >= 32) return;
        PcgNode* n = nodes[idx];
        switch (n->OutType()) {
        case PcgDataType::HeightMap:
            cached_ref.type       = PcgDataType::HeightMap;
            cached_ref.height_ptr = pcg_arena::height[idx];
            cached_ref.count      = PCG_FIELD_RES * PCG_FIELD_RES;
            break;
        case PcgDataType::Mask:
            cached_ref.type     = PcgDataType::Mask;
            cached_ref.mask_ptr = pcg_arena::mask[idx];
            cached_ref.count    = PCG_FIELD_RES * PCG_FIELD_RES;
            break;
        case PcgDataType::PointCloud:
            cached_ref.type        = PcgDataType::PointCloud;
            cached_ref.scatter_ptr = pcg_arena::scatter[idx];
            cached_ref.count       = 0;
            break;
        case PcgDataType::BiomeMap:
            cached_ref.type      = PcgDataType::BiomeMap;
            cached_ref.biome_ptr = pcg_arena::biome[idx];
            cached_ref.count     = PCG_FIELD_RES * PCG_FIELD_RES;
            break;
        default:
            break;
        }
    }
};

} // namespace md
