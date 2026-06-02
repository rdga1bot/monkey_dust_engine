#pragma once
// md_pool.h — Named typed fixed-size pool allocator.
//
// Port of AI.exe.c pattern: FUN_00995ab0(name, count, elem_size, align)
// Each subsystem declares its own pool with a debug name, fixed capacity,
// and element type. Zero malloc — all storage is inline.
//
// Usage:
//   static MdTypedPool<SuspiciousItem, 300> s_item_pool{"suspicious_item_data"};
//   SuspiciousItem* p = s_item_pool.Alloc();
//   s_item_pool.Free(p);
//   s_item_pool.Reset();  // bulk-free all (e.g. on level load)
//
// Constraints:
//   - T must be trivially destructible (no destructor call on Free/Reset)
//   - N ≤ 65535
//   - Thread-unsafe: call from a single thread (logic tick)

#include <cstdint>
#include <cstdio>

#ifndef MD_LOG_WARNING_POOL
#define MD_LOG_WARNING_POOL(n, cap) \
    fprintf(stderr, "[MdTypedPool] '%s' full (cap=%d)\n", (n), (cap))
#endif

template<typename T, int N, int Align = alignof(T)>
struct MdTypedPool {
    static_assert(N > 0 && N <= 65535, "pool size out of range");
    static_assert((Align & (Align - 1)) == 0, "Align must be power of two");

    const char* name = "unnamed";

    // ── Alloc / Free ────────────────────────────────────────────────────────
    T* Alloc() {
        if (free_head_ < 0) {
            MD_LOG_WARNING_POOL(name, N); // NOLINT — macro uses fprintf
            return nullptr;
        }
        const int idx = free_list_[free_head_--];
        used_count_++;
        return ptr(idx);
    }

    void Free(T* p) {
        if (!p) return;
        const int idx = (int)(p - ptr(0));
        if (idx < 0 || idx >= N) return;
        free_list_[++free_head_] = (uint16_t)idx;
        used_count_--;
    }

    void Reset() {
        free_head_ = N - 1;
        for (int i = 0; i < N; ++i) free_list_[i] = (uint16_t)i;
        used_count_ = 0;
    }

    int  UsedCount()  const { return used_count_; }
    int  FreeCount()  const { return N - used_count_; }
    int  Capacity()   const { return N; }
    bool Full()       const { return used_count_ == N; }

    // ── Iteration (over allocated slots) ────────────────────────────────────
    // Simple linear scan — use for debug/stats only; not for hot path.
    template<typename Fn>
    void ForEachUsed(Fn fn) const {
        bool in_free[N] = {};
        for (int i = 0; i <= free_head_; ++i) in_free[free_list_[i]] = true;
        for (int i = 0; i < N; ++i)
            if (!in_free[i]) fn(*ptr(i));
    }

    MdTypedPool() { Reset(); }
    explicit MdTypedPool(const char* n) : name(n) { Reset(); }

private:
    T* ptr(int i) { return reinterpret_cast<T*>(storage_) + i; }
    const T* ptr(int i) const { return reinterpret_cast<const T*>(storage_) + i; }

    alignas(Align) uint8_t storage_[N * sizeof(T)];
    uint16_t free_list_[N];
    int      free_head_  = N - 1;
    int      used_count_ = 0;
};
