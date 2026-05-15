#pragma once
#include <monkey_dust/nav/navmesh.h>
#include <monkey_dust/nav/path_cache.h>
#include <monkey_dust/nav/nav_async_queue.h>
#include <thread>
#include <atomic>

// ─────────────────────────────────────────────────────────
// NavSystem — singleton: NavMesh + PathCache + async worker.
//
// Threading model:
//   Main thread: QueryPath(), EnqueueRebuild(), CheckRebuildDone()
//   Worker:      reads NavRebuildRequest copies → RebuildTile()
//
// is_rebuilding_=true → QueryPath() returns 0 (uses stale cache)
// Worker finishes → is_rebuilding_=false, main picks up result.
//
// ⚠️ Recast/Detour is NOT thread-safe. Worker holds exclusive
//    access to navmesh_ while is_rebuilding_ is true.
// ─────────────────────────────────────────────────────────

class NavSystem {
public:
    static NavSystem& Get() {
        static NavSystem inst;
        return inst;
    }

    bool Init(const char* cache_path, float world_r = 200.0f);
    int QueryPath(float sx, float sz,
                  float ex, float ez,
                  float now_s,
                  float* out_pts3, int max_pts);

    // M47: LOD-aware query. Frozen tier: return cached path ignoring TTL,
    // or a single direct waypoint when no cache entry exists.
    // Close tier: identical to QueryPath.
    int QueryPathLod(float sx, float sz,
                     float ex, float ez,
                     float now_s,
                     NavLodTier tier,
                     float* out_pts3, int max_pts);

    // M47: freeze / unfreeze path in cache (bypasses TTL for Frozen NPCs).
    void FreezePath(float sx, float sz, float ex, float ez) noexcept;
    void UnfreezePath(float sx, float sz, float ex, float ez) noexcept;
    bool IsReady() const { return navmesh_.IsValid(); }
    const dtNavMesh* GetRawMesh() const {
        return const_cast<NavMesh&>(navmesh_).Raw();
    }
    bool RebuildTile(float wx, float wz,
                     const float* obs_verts, int nobs_verts,
                     const int*   obs_tris,  int nobs_tris);
    // Build NavMesh from a tile-map walkable mesh (replaces current navmesh).
    // Called by md::flare::BuildNavMeshFromTileMap() at map load time.
    bool BuildForTileMap(const float* verts, int nverts,
                         const int*   tris,  int ntris,
                         float cs = 0.5f, float ch = 0.2f);

    bool EnqueueRebuild(float wx, float wz,
                        const float* obs_verts, int nv,
                        const int*   obs_tris,  int nt);
    bool CheckRebuildDone();
    void StartWorker();
    void StopWorker();
    bool IsWorkerRunning() const { return worker_running_.load(); }
    bool HasPendingRebuild() const {
        return rebuild_queue_.head_.load() != rebuild_queue_.tail_.load();
    }

private:
    NavSystem() = default;

    NavMesh           navmesh_;
    PathCache         pathcache_;
    NavAsyncQueue     rebuild_queue_;
    std::thread       worker_;
    std::atomic<bool> worker_running_{false};
    std::atomic<bool> is_rebuilding_{false};

    void WorkerLoop();
};
