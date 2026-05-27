#pragma once
#include "pcg_graph.h"
#include "pcg_nodes_base.h"
#include <thread>
#include <atomic>

namespace md {

static constexpr int TILE_RES = PCG_FIELD_RES; // 256

// ── TileData — result of one PcgGraph execution for a terrain chunk ───────────
struct TileData {
    int   chunk_x = 0, chunk_z = 0;
    float world_size = 500.f;          // metres

    // Heightmap: TILE_RES × TILE_RES floats, row-major (z outer), metres
    float       heightmap[TILE_RES * TILE_RES] = {};

    // Biome IDs: TILE_RES × TILE_RES uint8
    uint8_t     biomemap [TILE_RES * TILE_RES] = {};

    // Scatter points from all PcgScatterNodes in the graph
    ScatterPoint scatter[PCG_MAX_SCATTER] = {};
    int          scatter_count = 0;

    uint32_t     graph_version = 0;    // PcgGraph::exec_version at build time
    bool         needs_apply   = false;
    bool         applied       = false;
};

// ── TerrainTileGen — worker-thread terrain rebuild pipeline ───────────────────
// Usage (main thread only, except WorkerFn which runs on the thread):
//   RequestRebuild(graph, cx, cz)  → launches background thread
//   PollApply()                    → returns true if new data is ready; copies to ready_
//   GetReady()                     → const ref to the fully-built TileData
//   IsBuilding()                   → true while thread is running
//
// Thread safety: pending_ is written ONLY by the worker thread after launch.
//                ready_  is written ONLY by PollApply() on the main thread.
//                building_ is atomic bool.
class TerrainTileGen {
public:
    static TerrainTileGen& Get() {
        static TerrainTileGen inst;
        return inst;
    }

    // Launches background rebuild. Ignored if already building.
    // `graph` must remain valid for the duration of the build (caller ensures this).
    void RequestRebuild(PcgGraph& graph, int chunk_x = 0, int chunk_z = 0,
                        float world_size = 500.f) {
        if (building_.load()) return;
        building_.store(true);
        result_ready_.store(false);
        if (worker_.joinable()) worker_.join();
        worker_ = std::thread(&TerrainTileGen::WorkerFn, this,
                              &graph, chunk_x, chunk_z, world_size);
    }

    // Call once per frame from main thread.
    // Returns true if new tile data became available this call.
    bool PollApply() {
        if (!result_ready_.load()) return false;
        if (worker_.joinable()) worker_.join();
        result_ready_.store(false);
        ready_ = pending_;   // copy BSS tile (512 KB — acceptable for a frame stall)
        ready_.needs_apply = false;
        ready_.applied     = true;
        return true;
    }

    bool IsBuilding() const { return building_.load(); }

    const TileData& GetReady() const { return ready_; }

private:
    TileData       pending_;
    TileData       ready_;
    std::thread    worker_;
    std::atomic<bool> building_    {false};
    std::atomic<bool> result_ready_{false};

    void WorkerFn(PcgGraph* graph, int cx, int cz, float world_size) {
        pending_ = TileData{};
        pending_.chunk_x    = cx;
        pending_.chunk_z    = cz;
        pending_.world_size = world_size;

        // Run the PcgGraph (all node Execute() calls happen here)
        graph->Execute();
        pending_.graph_version = graph->exec_version;

        // Copy heightmap from the Output node
        for (int i = 0; i < graph->node_count; ++i) {
            PcgNode* n = graph->nodes[i];
            if (!n || !n->alive) continue;
            if (auto* out = dynamic_cast<PcgOutputNode*>(n)) {
                if (out->final_height)
                    memcpy(pending_.heightmap, out->final_height,
                           TILE_RES * TILE_RES * sizeof(float));
                if (out->final_biome)
                    memcpy(pending_.biomemap, out->final_biome,
                           TILE_RES * TILE_RES);
            }
            // Collect scatter points from all scatter nodes
            if (auto* sc = dynamic_cast<PcgScatterNode*>(n)) {
                int copy = sc->cached.count;
                if (pending_.scatter_count + copy > PCG_MAX_SCATTER)
                    copy = PCG_MAX_SCATTER - pending_.scatter_count;
                if (copy > 0) {
                    memcpy(pending_.scatter + pending_.scatter_count,
                           sc->cached.scatter_ptr, copy * sizeof(ScatterPoint));
                    pending_.scatter_count += copy;
                }
            }
        }

        pending_.needs_apply = true;
        building_.store(false);
        result_ready_.store(true);
    }

    TerrainTileGen() = default;
    ~TerrainTileGen() { if (worker_.joinable()) worker_.join(); }
};

} // namespace md
