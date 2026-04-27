#pragma once
#include <atomic>
#include <cstring>

struct NavRebuildRequest {
    float wx, wz;
    float obs_verts[4 * 3];  // max 4 obstacle quad vertices (copy!)
    int   obs_tris [2 * 3];  // max 2 triangles (copy!)
    int   nobs_verts;        // 0 = demolish (remove obstacle)
    int   nobs_tris;
    bool  valid;
};

struct NavRebuildResult {
    bool success;
    bool valid;
};

// ─────────────────────────────────────────────────────────
// Lock-free SPSC ring buffer for NavMesh rebuild requests.
// Single producer (main thread) / single consumer (worker).
// All methods are wait-free for the respective caller.
// ─────────────────────────────────────────────────────────
class NavAsyncQueue {
public:
    static constexpr int CAPACITY = 8;

    // head_/tail_ public for HasPendingRebuild() in NavSystem
    std::atomic<int> head_{0}, tail_{0};

    bool TryEnqueue(const NavRebuildRequest& req) {
        int h    = head_.load(std::memory_order_relaxed);
        int next = (h + 1) % CAPACITY;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        requests_[h] = req;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool TryDequeue(NavRebuildRequest& out) {
        int t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        out = requests_[t];
        tail_.store((t + 1) % CAPACITY, std::memory_order_release);
        return true;
    }

    void PostResult(int /*slot*/, bool success) {
        int h    = res_head_.load(std::memory_order_relaxed);
        int next = (h + 1) % CAPACITY;
        if (next == res_tail_.load(std::memory_order_acquire)) return;
        NavRebuildResult r = {};
        r.success = success;
        r.valid   = true;
        results_[h] = r;
        res_head_.store(next, std::memory_order_release);
    }

    bool TryGetResult(NavRebuildResult& out) {
        int t = res_tail_.load(std::memory_order_relaxed);
        if (t == res_head_.load(std::memory_order_acquire)) return false;
        out = results_[t];
        res_tail_.store((t + 1) % CAPACITY, std::memory_order_release);
        return true;
    }

private:
    NavRebuildRequest requests_[CAPACITY];
    NavRebuildResult  results_ [CAPACITY];
    std::atomic<int> res_head_{0}, res_tail_{0};
};
