#pragma once
// gpu_frame_timeline.h — CPU-side frame timeline tracker.
//
// Vulkan adaptation: VkTimelineSemaphore provides monotonic GPU progress counter.
// SDL_GPU manages semaphores internally; we can't inject timeline semaphores.
// This module provides the CPU-side equivalent:
//   - Monotonic frame counter (never wraps in reasonable session)
//   - CPU timestamp per frame for CPU-GPU overlap measurement
//   - Latency tracking: submit_time → fence_wait_time gap = frame latency
//
// Usage:
//   GpuFrameTimeline::Get().OnSubmit();          // after SDL_SubmitGPUCommandBuffer
//   GpuFrameTimeline::Get().OnFenceSignaled();   // after SDL_WaitForGPUFences
//   GpuFrameTimeline::Get().FrameIndex();        // monotonic frame counter
//   GpuFrameTimeline::Get().AvgGpuLatencyMs();  // rolling average latency

#include <cstdint>
#include <cstdio>
#if defined(__linux__)
#  include <time.h>
#endif

class GpuFrameTimeline {
public:
    static GpuFrameTimeline& Get() {
        static GpuFrameTimeline inst;
        return inst;
    }

    // Call immediately after submitting a command buffer to GPU.
    void OnSubmit() {
        submit_ns_[slot_] = NowNs();
        frame_index_++;
    }

    // Call when the previous frame's fence has been signaled (GPU finished).
    void OnFenceSignaled() {
        const uint64_t now = NowNs();
        const uint64_t lat = now - submit_ns_[slot_];
        latency_ns_[head_] = lat;
        head_ = (head_ + 1) & (HIST - 1);
        if (count_ < HIST) ++count_;
        slot_ = (slot_ + 1) & 1;  // alternate between 2 CPU timestamps
    }

    uint64_t FrameIndex()        const { return frame_index_; }

    float AvgGpuLatencyMs() const {
        if (count_ == 0) return 0.f;
        uint64_t sum = 0;
        for (int i = 0; i < count_; ++i) sum += latency_ns_[i];
        return (float)((double)sum / (double)count_) / 1e6f;
    }

    float MaxGpuLatencyMs() const {
        if (count_ == 0) return 0.f;
        uint64_t mx = 0;
        for (int i = 0; i < count_; ++i) if (latency_ns_[i] > mx) mx = latency_ns_[i];
        return (float)mx / 1e6f;
    }

    void PrintStats() const {
        fprintf(stdout, "[FrameTimeline] frame=%llu  avg_gpu=%.2fms  max_gpu=%.2fms\n",
                (unsigned long long)frame_index_,
                AvgGpuLatencyMs(), MaxGpuLatencyMs());
    }

private:
    static constexpr int HIST = 64;

    static uint64_t NowNs() {
#if defined(__linux__)
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#else
        return 0;
#endif
    }

    uint64_t frame_index_  = 0;
    uint64_t submit_ns_[2] = {};       // double-buffered submit timestamps
    int      slot_         = 0;
    uint64_t latency_ns_[HIST] = {};
    int      head_  = 0;
    int      count_ = 0;
};
