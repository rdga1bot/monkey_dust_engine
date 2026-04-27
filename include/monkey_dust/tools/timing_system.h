#pragma once

#ifdef DEBUG

#include "raylib.h"
#include <cstring>

static constexpr int MAX_TIMINGS = 16;

static constexpr struct { const char* name; float threshold_ms; } TIMING_THRESHOLDS[] = {
    {"AISystem",     3.0f},
    {"CombatSystem", 2.0f},
    {"NavMesh",      5.0f},
    {"Logic",        8.0f},
    {"BuildTick",    2.0f},
    {"UpdateLOD",    1.0f},
};

struct TimingEntry {
    char   name[32];
    float  avg_ms;
    float  max_ms;
    int    samples;
    float  sum;
    double begin_s;
};

class TimingSystem {
public:
    static TimingSystem& Get() { static TimingSystem inst; return inst; }

    void Begin(const char* name) {
        int idx = FindOrCreate(name);
        if (idx >= 0) entries_[idx].begin_s = GetTime();
    }

    void End(const char* name) {
        int idx = Find(name);
        if (idx < 0) return;
        float elapsed = (float)((GetTime() - entries_[idx].begin_s) * 1000.0);
        entries_[idx].sum += elapsed;
        entries_[idx].samples++;
        if (elapsed > entries_[idx].max_ms) entries_[idx].max_ms = elapsed;
        entries_[idx].avg_ms = entries_[idx].sum / (float)entries_[idx].samples;
    }

    void Report() {
        for (int i = 0; i < count_; ++i) {
            float thresh = Threshold(entries_[i].name);
            if (thresh > 0.0f && entries_[i].avg_ms > thresh)
                TraceLog(LOG_WARNING,
                         "[Timing] %s avg=%.2fms max=%.2fms threshold=%.1fms",
                         entries_[i].name, entries_[i].avg_ms,
                         entries_[i].max_ms, thresh);
        }
        // Reset rolling window each report cycle
        for (int i = 0; i < count_; ++i) {
            entries_[i].sum     = 0.0f;
            entries_[i].samples = 0;
            entries_[i].avg_ms  = 0.0f;
            entries_[i].max_ms  = 0.0f;
        }
    }

    int                Count()       const { return count_; }
    const TimingEntry& GetAt(int i)  const { return entries_[i]; }

private:
    TimingSystem() : count_(0) { memset(entries_, 0, sizeof(entries_)); }

    TimingEntry entries_[MAX_TIMINGS];
    int         count_;

    int Find(const char* name) const {
        for (int i = 0; i < count_; ++i)
            if (strncmp(entries_[i].name, name, 31) == 0) return i;
        return -1;
    }

    int FindOrCreate(const char* name) {
        int idx = Find(name);
        if (idx >= 0) return idx;
        if (count_ >= MAX_TIMINGS) return -1;
        idx = count_++;
        memset(&entries_[idx], 0, sizeof(entries_[idx]));
        strncpy(entries_[idx].name, name, 31);
        return idx;
    }

    float Threshold(const char* name) const {
        for (auto& t : TIMING_THRESHOLDS)
            if (strncmp(name, t.name, 31) == 0) return t.threshold_ms;
        return 0.0f;
    }
};

#define TIMING_BEGIN(name) TimingSystem::Get().Begin(name)
#define TIMING_END(name)   TimingSystem::Get().End(name)

#else

#define TIMING_BEGIN(name) ((void)0)
#define TIMING_END(name)   ((void)0)

#endif // DEBUG
