#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
#ifdef __x86_64__
#  include <immintrin.h>  // _mm_pause
#endif

// ─────────────────────────────────────────────────────────
// PathCache — LRU кеш шляхів для Recast/Detour запитів.
//
// Якщо 10 NPC йдуть в одне місце — рахуємо шлях 1 раз.
// Ключ:  (start_cell, end_cell) в SpatialGrid координатах.
// Розмір: MAX_CACHED_PATHS = 64 (фіксований масив).
// TTL:    5 секунд (шлях не вічний — NPC блокують).
//
// Зберігає до MAX_PATH_LEN вершин шляху (Vector3-like: x,y,z).
// ─────────────────────────────────────────────────────────

static constexpr int   MAX_CACHED_PATHS = 64;
static constexpr int   MAX_PATH_LEN     = 64;
static constexpr float PATH_TTL_S       = 5.0f;

struct CachedPath {
    uint32_t key_start;          // хеш стартової позиції (по сітці)
    uint32_t key_end;            // хеш цільової позиції
    float    verts[MAX_PATH_LEN * 3]; // x,y,z вершини шляху
    int      len;                // кількість вершин
    float    timestamp_s;        // час запису
    bool     valid;
};

class PathCache {
public:
    PathCache() { Clear(); }

    void Clear() {
        AcquireLock();
        for (auto& e : entries_) e.valid = false;
        lru_clock_ = 0;
        ReleaseLock();
    }

    // Пошук шляху в кеші. Повертає true якщо знайдено і не протухло.
    bool Get(uint32_t key_start, uint32_t key_end, float now_s,
             float* out_verts, int& out_len) const
    {
        AcquireLock();
        for (const auto& e : entries_) {
            if (!e.valid) continue;
            if (e.key_start != key_start || e.key_end != key_end) continue;
            if ((now_s - e.timestamp_s) > PATH_TTL_S) continue;
            memcpy(out_verts, e.verts, e.len * 3 * sizeof(float));
            out_len = e.len;
            ReleaseLock();
            return true;
        }
        ReleaseLock();
        return false;
    }

    // Записати шлях у кеш (витісняє найстаріший запис).
    void Put(uint32_t key_start, uint32_t key_end, float now_s,
             const float* verts, int len)
    {
        if (len <= 0 || len > MAX_PATH_LEN) return;
        AcquireLock();
        int slot = FindSlot(key_start, key_end, now_s);
        CachedPath& e = entries_[slot];
        e.key_start   = key_start;
        e.key_end     = key_end;
        e.timestamp_s = now_s;
        e.len         = len;
        e.valid       = true;
        memcpy(e.verts, verts, len * 3 * sizeof(float));
        ReleaseLock();
    }

    // Хеш для world-позиції (квантується по CELL_SIZE сітці)
    static uint32_t PosKey(float wx, float wz) {
        int cx = (int)((wx + 500.0f) / 20.0f);
        int cz = (int)((wz + 500.0f) / 20.0f);
        return (uint32_t)(cx & 0xFFFF) | ((uint32_t)(cz & 0xFFFF) << 16);
    }

private:
    CachedPath entries_[MAX_CACHED_PATHS];
    int        lru_clock_;
    mutable std::atomic<bool> lock_{false};

    void AcquireLock() const {
        bool expected = false;
        while (!lock_.compare_exchange_weak(
                   expected, true,
                   std::memory_order_acquire,
                   std::memory_order_relaxed)) {
            expected = false;
#ifdef __x86_64__
            _mm_pause(); // yield HT sibling core during contention
#endif
        }
    }
    void ReleaseLock() const {
        lock_.store(false, std::memory_order_release);
    }

    int FindSlot(uint32_t ks, uint32_t ke, float now_s) {
        // 1. Той самий ключ?
        for (int i = 0; i < MAX_CACHED_PATHS; ++i)
            if (entries_[i].valid &&
                entries_[i].key_start == ks &&
                entries_[i].key_end   == ke)
                return i;

        // 2. Порожня комірка?
        for (int i = 0; i < MAX_CACHED_PATHS; ++i)
            if (!entries_[i].valid)
                return i;

        // 3. Найстаріший запис
        int oldest = 0;
        for (int i = 1; i < MAX_CACHED_PATHS; ++i)
            if (entries_[i].timestamp_s < entries_[oldest].timestamp_s)
                oldest = i;
        return oldest;
    }
};
