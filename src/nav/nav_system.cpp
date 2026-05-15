#include <monkey_dust/nav/nav_system.h>
#include <monkey_dust/tools/timing_system.h>
#include <cstdio>
#include <cstring>
#include <chrono>

bool NavSystem::Init(const char* cache_path, float world_r) {
    if (navmesh_.LoadFromFile(cache_path)) {
        fprintf(stdout, "[NavSystem] Loaded from %s\n", cache_path);
        return true;
    }
    fprintf(stdout, "[NavSystem] Building flat navmesh %.0fx%.0fm …\n",
            world_r * 2, world_r * 2);
    float verts[12] = {
        -world_r, 0.0f, -world_r,
         world_r, 0.0f, -world_r,
         world_r, 0.0f,  world_r,
        -world_r, 0.0f,  world_r,
    };
    int tris[6] = { 0, 3, 2,  0, 2, 1 };
    if (!navmesh_.Build(verts, 4, tris, 2, 1.0f, 0.5f)) {
        fprintf(stderr, "[NavSystem] Build FAILED\n");
        return false;
    }
    if (navmesh_.SaveToFile(cache_path))
        fprintf(stdout, "[NavSystem] Saved to %s\n", cache_path);
    else
        fprintf(stderr, "[NavSystem] Warning: cannot save %s\n", cache_path);
    return true;
}

int NavSystem::QueryPath(float sx, float sz,
                          float ex, float ez,
                          float now_s,
                          float* out_pts3, int max_pts)
{
    if (!navmesh_.IsValid() || max_pts <= 0) return 0;
    if (is_rebuilding_.load(std::memory_order_acquire)) return 0;

    uint32_t ks = PathCache::PosKey(sx, sz);
    uint32_t ke = PathCache::PosKey(ex, ez);

    int len = 0;
    if (pathcache_.Get(ks, ke, now_s, out_pts3, len) && len > 0)
        return len;

    int pts = navmesh_.FindPath(sx, 0.0f, sz,
                                ex, 0.0f, ez,
                                out_pts3, max_pts);
    if (pts > 0)
        pathcache_.Put(ks, ke, now_s, out_pts3, pts);

    return pts;
}

int NavSystem::QueryPathLod(float sx, float sz,
                             float ex, float ez,
                             float now_s,
                             NavLodTier tier,
                             float* out_pts3, int max_pts)
{
    if (max_pts <= 0) return 0;

    if (tier == NavLodTier::Frozen) {
        // Frozen NPCs: use cached path ignoring TTL; skip full recompute.
        uint32_t ks = PathCache::PosKey(sx, sz);
        uint32_t ke = PathCache::PosKey(ex, ez);
        int len = 0;
        if (pathcache_.Get(ks, ke, now_s, out_pts3, len) && len > 0)
            return len;
        // No cache: direct waypoint toward destination.
        out_pts3[0] = ex;
        out_pts3[1] = 0.0f;
        out_pts3[2] = ez;
        return 1;
    }

    return QueryPath(sx, sz, ex, ez, now_s, out_pts3, max_pts);
}

void NavSystem::FreezePath(float sx, float sz, float ex, float ez) noexcept {
    pathcache_.Freeze(PathCache::PosKey(sx, sz), PathCache::PosKey(ex, ez));
}

void NavSystem::UnfreezePath(float sx, float sz, float ex, float ez) noexcept {
    pathcache_.Unfreeze(PathCache::PosKey(sx, sz), PathCache::PosKey(ex, ez));
}

bool NavSystem::BuildForTileMap(const float* verts, int nverts,
                                 const int*   tris,  int ntris,
                                 float cs, float ch)
{
    pathcache_.Clear();
    bool ok = navmesh_.BuildTileMap(verts, nverts, tris, ntris, cs, ch);
    if (ok)
        fprintf(stdout, "[NavSystem] Tile NavMesh built (%d verts, %d tris)\n",
                nverts, ntris);
    else
        fprintf(stderr, "[NavSystem] Tile NavMesh build FAILED\n");
    return ok;
}

bool NavSystem::RebuildTile(float wx, float wz,
                             const float* obs_verts, int nobs_verts,
                             const int*   obs_tris,  int nobs_tris)
{
    pathcache_.Clear();
    return navmesh_.RebuildTile(wx, wz, obs_verts, nobs_verts,
                                obs_tris, nobs_tris);
}

bool NavSystem::EnqueueRebuild(float wx, float wz,
                                const float* obs_verts, int nv,
                                const int*   obs_tris,  int nt)
{
    NavRebuildRequest req = {};
    req.wx = wx;  req.wz = wz;
    req.nobs_verts = nv;
    req.nobs_tris  = nt;
    req.valid = true;
    if (nv > 0 && obs_verts) {
        int cv = nv < 4 ? nv : 4;
        memcpy(req.obs_verts, obs_verts, (size_t)cv * 3 * sizeof(float));
    }
    if (nt > 0 && obs_tris) {
        int ct = nt < 2 ? nt : 2;
        memcpy(req.obs_tris, obs_tris, (size_t)ct * 3 * sizeof(int));
    }
    return rebuild_queue_.TryEnqueue(req);
}

bool NavSystem::CheckRebuildDone() {
    NavRebuildResult res;
    if (rebuild_queue_.TryGetResult(res) && res.valid) {
        pathcache_.Clear();
        fprintf(stdout, "[NavSystem] Async rebuild %s\n",
                res.success ? "OK" : "FAILED");
        return true;
    }
    return false;
}

void NavSystem::StartWorker() {
    worker_running_.store(true);
    worker_ = std::thread([this] { WorkerLoop(); });
}

void NavSystem::StopWorker() {
    worker_running_.store(false);
    if (worker_.joinable()) worker_.join();
}

void NavSystem::WorkerLoop() {
    while (worker_running_.load(std::memory_order_relaxed)) {
        NavRebuildRequest req;
        if (rebuild_queue_.TryDequeue(req)) {
            is_rebuilding_.store(true, std::memory_order_release);
            TIMING_BEGIN("NavRebuild");
            bool ok = navmesh_.RebuildTile(
                req.wx, req.wz,
                req.nobs_verts > 0 ? req.obs_verts : nullptr,
                req.nobs_verts,
                req.nobs_tris  > 0 ? req.obs_tris  : nullptr,
                req.nobs_tris);
            TIMING_END("NavRebuild");
            if (ok) pathcache_.Clear();
            rebuild_queue_.PostResult(0, ok);
            is_rebuilding_.store(false, std::memory_order_release);
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
}
