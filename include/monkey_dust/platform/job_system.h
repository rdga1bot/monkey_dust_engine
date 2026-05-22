#pragma once
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_mutex.h>
#include <cstdint>

// VBfA-pattern job queue: CPU_count-1 worker threads, 100-slot circular buffer.
//
// Constants from VBfA binary (viking.exe.c):
//   max_queued_jobs = 100    (line 390933)
//   worker_threads  = CPU-1  (line 390815)
//   stack_per_thread = 64KB  (line 390792)
//
// Thread safety:
//   Submit() — main thread only during frame.
//   Flush()  — main thread; blocks until all in-flight jobs complete.
//   Workers  — call fn(data) concurrently; jobs must not touch EnTT registry
//              or GPU resources. Safe for: animation eval (T2 tier), path eval,
//              sense distance queries.
//
// Usage:
//   JobSystem::Get().Init();          // once at startup
//   JobSystem::Get().Submit(fn, ptr); // during frame
//   JobSystem::Get().Flush();         // before reading results / uploading GPU
//   JobSystem::Get().Shutdown();      // at exit
struct JobSystem {
    static constexpr int MAX_JOBS    = 100;
    static constexpr int MAX_WORKERS = 15;  // hard cap; actual = min(CPU-1, MAX_WORKERS)

    struct Job { void (*fn)(void*); void* data; };

    static JobSystem& Get();

    void Init();
    void Shutdown();

    // Submit a job. Blocks briefly if queue is full (back-pressure, rare in practice).
    void Submit(void (*fn)(void*), void* data);

    // Wait until every submitted job has completed.
    void Flush();

    int  NumWorkers() const { return num_workers_; }

private:
    static int SDLCALL s_worker_entry(void* self);
    void worker_loop();

    Job           buf_[MAX_JOBS]         = {};
    int           head_                  = 0;
    int           tail_                  = 0;
    int           count_                 = 0;  // jobs in queue
    int           inflight_              = 0;  // jobs queued + executing
    bool          quit_                  = false;
    int           num_workers_           = 0;

    SDL_Mutex*     mtx_      = nullptr;
    SDL_Condition* cv_work_  = nullptr;  // workers wait here
    SDL_Condition* cv_done_  = nullptr;  // Flush() waits here
    SDL_Condition* cv_cap_   = nullptr;  // Submit() waits if full
    SDL_Thread*    threads_[MAX_WORKERS] = {};
};
