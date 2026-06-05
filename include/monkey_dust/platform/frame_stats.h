#pragma once
#include <chrono>
#include <cstdio>
#include <cstring>

// FrameStats — lightweight Release-safe frame time breakdown.
// Usage:
//   FrameStats::Get().Begin("NpcRender");
//   ... work ...
//   FrameStats::Get().End("NpcRender");
//   FrameStats::Get().EndFrame(dt);  // once per frame

static constexpr int FS_MAX_SLOTS = 12;
static constexpr float FS_REPORT_INTERVAL = 5.f;  // seconds

struct FrameSlot {
    char  name[24];
    float sum_ms;
    float max_ms;
    int   frames;
    double begin_s;
};

class FrameStats {
public:
    static FrameStats& Get() { static FrameStats inst; return inst; }

    void Begin(const char* name) {
        int i = FindOrCreate(name);
        if (i >= 0) slots_[i].begin_s = Now();
    }

    void End(const char* name) {
        int i = Find(name);
        if (i < 0) return;
        float ms = (float)((Now() - slots_[i].begin_s) * 1000.0);
        slots_[i].sum_ms += ms;
        if (ms > slots_[i].max_ms) slots_[i].max_ms = ms;
        slots_[i].frames++;
    }

    // Call once per frame with frame dt (seconds).
    // Returns formatted string when report fires (every 5s), else nullptr.
    const char* EndFrame(float dt, int fps, int npc_count) {
        accum_ += dt;
        frame_count_++;
        if (accum_ < FS_REPORT_INTERVAL) return nullptr;

        char* p = report_buf_;
        p += snprintf(p, 256, "[PERF] %d FPS | NPCs=%d | ", fps, npc_count);
        for (int i = 0; i < count_; ++i) {
            float avg = slots_[i].frames > 0
                      ? slots_[i].sum_ms / (float)slots_[i].frames : 0.f;
            p += snprintf(p, (size_t)(report_buf_ + sizeof(report_buf_) - p),
                          "%s=%.1fms(max%.1f) ", slots_[i].name, avg, slots_[i].max_ms);
            slots_[i].sum_ms = 0.f;
            slots_[i].max_ms = 0.f;
            slots_[i].frames = 0;
        }
        accum_ = 0.f;
        frame_count_ = 0;
        fprintf(stderr, "%s\n", report_buf_);
        return report_buf_;
    }

    int Count() const { return count_; }
    const FrameSlot& GetAt(int i) const { return slots_[i]; }

private:
    FrameStats() : count_(0), accum_(0.f), frame_count_(0) {
        memset(slots_, 0, sizeof(slots_));
        memset(report_buf_, 0, sizeof(report_buf_));
    }

    FrameSlot slots_[FS_MAX_SLOTS];
    int       count_;
    float     accum_;
    int       frame_count_;
    char      report_buf_[512];

    static double Now() {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    int Find(const char* name) const {
        for (int i = 0; i < count_; ++i)
            if (strncmp(slots_[i].name, name, 23) == 0) return i;
        return -1;
    }
    int FindOrCreate(const char* name) {
        int i = Find(name);
        if (i >= 0) return i;
        if (count_ >= FS_MAX_SLOTS) return -1;
        i = count_++;
        memset(&slots_[i], 0, sizeof(slots_[i]));
        strncpy(slots_[i].name, name, 23);
        return i;
    }
};

#define FS_BEGIN(name) FrameStats::Get().Begin(name)
#define FS_END(name)   FrameStats::Get().End(name)
