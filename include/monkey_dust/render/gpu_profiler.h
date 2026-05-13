#pragma once
#include <cstdint>

// GpuProfiler — CPU-side wall-clock timing for named render passes.
// SDL_GPU does not expose timestamp queries on all backends; CPU time is a
// reasonable proxy on single-GPU hardware (Intel HD 520 target).
// Call BeginFrame() once per render frame, then BeginPass/EndPass around
// each major render pass. Results from the prior frame are readable via
// GetResult() / TotalMs().
namespace md {

class GpuProfiler {
public:
    static GpuProfiler& Get() { static GpuProfiler i; return i; }

    static constexpr int MAX_PASSES = 16;

    void BeginFrame();
    void BeginPass(const char* name);
    void EndPass();

    struct PassResult {
        char  name[32] = {};
        float cpu_ms   = 0.0f;
    };

    int               ResultCount() const { return result_count_; }
    const PassResult& GetResult(int i)  const { return results_[i]; }
    float             TotalMs()         const { return total_ms_;   }

private:
    GpuProfiler() = default;

    uint64_t NowUs() const;

    struct PendingPass { char name[32]; uint64_t begin_us; };
    PendingPass pending_[MAX_PASSES] = {};
    int         pass_count_          = 0;  // open passes in current frame

    PassResult  results_[MAX_PASSES] = {};
    int         result_count_        = 0;
    float       total_ms_            = 0.0f;
};

} // namespace md
