#pragma once
#include <monkey_dust/world/chunk_def.h>
#include <monkey_dust/world/chunk_load_staging.h>
#include <monkey_dust/ecs/md_registry.h>
#include <thread>
#include <atomic>

// Callbacks registered by game to spawn/save/destroy entities.
// Engine only handles IO scheduling and chunk lifecycle — no game components.
using ChunkSpawnTreeFn    = void(*)(MdRegistry&, const StagedTree& t);
using ChunkSpawnNpcFn     = void(*)(MdRegistry&, const StagedNpc& n);
using ChunkDestroyEntityFn= void(*)(MdRegistry&, MdEntity e);
using ChunkSaveEntityFn   = void(*)(MdRegistry&, MdEntity e, void* file);

class ChunkManager {
public:
    static ChunkManager& Get() { static ChunkManager inst; return inst; }

    void Init(const char* chunks_dir);
    void Update(float player_x, float player_z);
    void UnloadAll();

    // CATHODE RE §7.9: streaming budget cap.
    // Sets max bytes that may be applied from staging per Update() call.
    // When budget exceeded, remaining staged chunks are deferred to next Update().
    // Default: 2MB/frame (safe on Intel HD 520 with 4-8GB shared RAM).
    // "Not enough streaming budget to load other side of door, opening anyway!" (AI.exe line 178065)
    // VBfA line 12623: 2-sample terrain height query for multi-floor buildings/bridges.
    // Primary: (x, z), Secondary: (x, z - 0.96) — returns higher of the two surfaces.
    static float GetHeightAt(float world_x, float world_z);
    static float GetHeightAtWithSecondary(float world_x, float world_z) {
        float primary   = GetHeightAt(world_x, world_z);
        float secondary = GetHeightAt(world_x, world_z - 0.96f);
        return primary > secondary ? primary : secondary;
    }

    void SetStreamingBudgetBytes(int bytes_per_update) { stream_budget_ = bytes_per_update; }
    int  StreamingBudgetBytes() const { return stream_budget_; }
    int  BytesStreamedLastUpdate() const { return bytes_last_update_; }

    // B-6: explicit on-demand chunk request (interior portals, teleports).
    // Async: enqueues into the IO worker queue; Apply happens on next ApplyStagedChunks().
    void RequestChunkLoad(ChunkCoord c) { TryEnqueuePending(c); }

    void StartIOWorker();
    void StopIOWorker();
    bool IsIOWorkerRunning() const { return io_running_.load(); }

    void ApplyStagedChunks();

    // Game registers these before Init()
    void SetSpawnCallbacks(ChunkSpawnTreeFn tree_fn, ChunkSpawnNpcFn npc_fn,
                           ChunkDestroyEntityFn destroy_fn, ChunkSaveEntityFn save_fn) {
        spawn_tree_fn_    = tree_fn;
        spawn_npc_fn_     = npc_fn;
        destroy_entity_fn_= destroy_fn;
        save_entity_fn_   = save_fn;
    }

private:
    ChunkManager() : active_count_(0), last_player_chunk_({9999, 9999}) {
        chunks_dir_[0] = '\0';
        for (int i = 0; i < MAX_CHUNKS_ACTIVE; ++i) {
            active_[i].stream_state = ChunkStreamState::Inactive;
            active_[i].entity_count = 0;
        }
        for (int i = 0; i < MAX_STAGING; ++i)
            staging_[i].consumed = true;
    }

    ChunkData  active_[MAX_CHUNKS_ACTIVE];
    int        active_count_;
    char       chunks_dir_[128];
    ChunkCoord last_player_chunk_;

    ChunkLoadStaging  staging_[MAX_STAGING];
    std::thread       io_worker_;
    std::atomic<bool> io_running_{false};

    struct PendingCoord { int x, z; bool valid; };
    PendingCoord     pending_[MAX_STAGING];
    std::atomic<int> pending_head_{0}, pending_tail_{0};

    ChunkSpawnTreeFn     spawn_tree_fn_     = nullptr;
    ChunkSpawnNpcFn      spawn_npc_fn_      = nullptr;
    ChunkDestroyEntityFn destroy_entity_fn_ = nullptr;
    ChunkSaveEntityFn    save_entity_fn_    = nullptr;

    // CATHODE RE §7.9: streaming budget
    int stream_budget_     = 2 * 1024 * 1024;  // 2 MB default
    int bytes_last_update_ = 0;

    bool TryEnqueuePending(ChunkCoord c);
    bool TryDequeuePending(ChunkCoord& out);
    void IOWorkerLoop();
    void LoadChunkFromStaging(ChunkLoadStaging& stg);
    void UnloadChunk(int idx);
    bool SaveChunk(int idx);
    int  FindActive(ChunkCoord coord) const;
};
