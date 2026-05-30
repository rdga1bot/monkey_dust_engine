#include <monkey_dust/platform/job_system.h>
#include <SDL3/SDL_cpuinfo.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <monkey_dust/platform/md_log.h>
#ifdef __linux__
#  include <pthread.h>
#  include <SDL3/SDL_thread.h>
#endif

JobSystem& JobSystem::Get() {
    static JobSystem s;
    return s;
}

// ── Worker thread entry ───────────────────────────────────────────────────────

int SDLCALL JobSystem::s_worker_entry(void* self) {
    static_cast<JobSystem*>(self)->worker_loop();
    return 0;
}

void JobSystem::worker_loop() {
    while (true) {
        SDL_LockMutex(mtx_);
        while (!quit_ && count_ == 0)
            SDL_WaitCondition(cv_work_, mtx_);
        if (quit_) { SDL_UnlockMutex(mtx_); return; }

        Job job   = buf_[tail_];
        tail_     = (tail_ + 1) % MAX_JOBS;
        --count_;
        SDL_BroadcastCondition(cv_cap_);  // slot freed — unblock a waiting Submit
        SDL_UnlockMutex(mtx_);

        job.fn(job.data);

        SDL_LockMutex(mtx_);
        if (--inflight_ == 0)
            SDL_BroadcastCondition(cv_done_);
        SDL_UnlockMutex(mtx_);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

// ── Config loader (VBfA Tasks.txt format) ────────────────────────────────────
void JobSystem::LoadFromCfg(const char* path) {
    FILE* f = ::fopen(path, "r");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "[JobSystem] tasks.cfg not found at '%s', using defaults", path);
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[64]; int val = 0;
        if (sscanf(line, "%63s %d", key, &val) == 2) {
            if (strcmp(key, "worker_threads") == 0 && val > 0)
                cfg_worker_override_ = val;
            else if (strcmp(key, "batch_size") == 0 && val > 0)
                batch_size_ = val;
        }
    }
    fclose(f);
    MD_LOG(MD_LOG_INFO, "[JobSystem] tasks.cfg: worker_threads=%d batch_size=%d",
           cfg_worker_override_, batch_size_);
}

void JobSystem::Init() {
    mtx_     = SDL_CreateMutex();
    cv_work_ = SDL_CreateCondition();
    cv_done_ = SDL_CreateCondition();
    cv_cap_  = SDL_CreateCondition();

    int cores = SDL_GetNumLogicalCPUCores();
    if (cfg_worker_override_ > 0)
        num_workers_ = std::min(cfg_worker_override_, MAX_WORKERS);
    else
        num_workers_ = std::min(std::max(cores - 1, 1), MAX_WORKERS);

    for (int i = 0; i < num_workers_; ++i) {
        char name[16];
        SDL_snprintf(name, sizeof(name), "JobWorker%d", i);
        threads_[i] = SDL_CreateThread(s_worker_entry, name, this);

#ifdef __linux__
        // VBfA RE §8 + CATHODE RE §7: pin workers to physical cores.
        // Worker i gets core (i+1) % num_cores (core 0 = main thread).
        {
            pthread_t tid = (pthread_t)SDL_GetThreadID(threads_[i]);
            cpu_set_t cs;
            CPU_ZERO(&cs);
            CPU_SET((i + 1) % cores, &cs);
            pthread_setaffinity_np(tid, sizeof(cs), &cs);
        }
#endif
    }
    MD_LOG(MD_LOG_INFO, "[JobSystem] %d workers, batch_size=%d (SDL %d logical cores)",
           num_workers_, batch_size_, cores);
}

void JobSystem::Shutdown() {
    SDL_LockMutex(mtx_);
    quit_ = true;
    SDL_BroadcastCondition(cv_work_);
    SDL_UnlockMutex(mtx_);

    for (int i = 0; i < num_workers_; ++i)
        SDL_WaitThread(threads_[i], nullptr);

    SDL_DestroyMutex(mtx_);
    SDL_DestroyCondition(cv_work_);
    SDL_DestroyCondition(cv_done_);
    SDL_DestroyCondition(cv_cap_);
}

void JobSystem::Submit(void (*fn)(void*), void* data) {
    SDL_LockMutex(mtx_);
    while (count_ >= MAX_JOBS)           // back-pressure: queue full
        SDL_WaitCondition(cv_cap_, mtx_);
    buf_[head_] = {fn, data};
    head_       = (head_ + 1) % MAX_JOBS;
    ++count_;
    ++inflight_;
    SDL_BroadcastCondition(cv_work_);
    SDL_UnlockMutex(mtx_);
}

void JobSystem::Flush() {
    SDL_LockMutex(mtx_);
    while (inflight_ > 0)
        SDL_WaitCondition(cv_done_, mtx_);
    SDL_UnlockMutex(mtx_);
}
