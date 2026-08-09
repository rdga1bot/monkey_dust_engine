#include <monkey_dust/render/gpu_profiler.h>
#include <cstring>
#include <SDL3/SDL.h>

namespace md {

uint64_t GpuProfiler::NowUs() const {
    uint64_t cnt  = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    return freq > 0 ? (cnt * 1000000u / freq) : 0u;
}

void GpuProfiler::BeginFrame() {
    // Swap completed passes to results_ for the editor to read.
    result_count_ = pass_count_;
    total_ms_     = 0.0f;
    for (int i = 0; i < result_count_; ++i)
        total_ms_ += results_[i].cpu_ms;
    pass_count_ = 0;
}

void GpuProfiler::BeginPass(const char* name) {
    if (pass_count_ >= MAX_PASSES) return;
    PendingPass& p = pending_[pass_count_];
    strncpy(p.name, name, sizeof(p.name) - 1);
    p.name[sizeof(p.name) - 1] = '\0';
    p.begin_us = NowUs();
}

void GpuProfiler::EndPass() {
    if (pass_count_ >= MAX_PASSES) return;
    uint64_t end_us = NowUs();
    PendingPass& p  = pending_[pass_count_];
    PassResult&  r  = results_[pass_count_];
    strncpy(r.name, p.name, sizeof(r.name) - 1);
    r.name[sizeof(r.name) - 1] = '\0';
    r.cpu_ms = (end_us >= p.begin_us) ? float(end_us - p.begin_us) * 0.001f : 0.0f;
    ++pass_count_;
}

} // namespace md
