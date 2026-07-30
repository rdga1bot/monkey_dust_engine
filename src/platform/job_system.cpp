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
    // SDL_SetCurrentThreadPriority sets the *calling* thread's priority.
    // worker_roles_ index is determined by matching thread ID.
    {
        SDL_ThreadID my_id = SDL_GetCurrentThreadID();
        for (int i = 0; i < num_workers_; ++i) {
            if (threads_[i] && SDL_GetThreadID(threads_[i]) == my_id) {
                int prio = kPriorityTable[(int)worker_roles_[i]];
                SDL_SetCurrentThreadPriority((SDL_ThreadPriority)prio);
                break;
            }
        }
    }

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
        // Naughty Dog fiber pattern: decrement dependency counter if set.
        if (job.counter) SDL_AtomicDecRef(job.counter);

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

constexpr int JobSystem::kPriorityTable[JobSystem::RoleCount];

void JobSystem::SetWorkerRole(int idx, WorkerRole role) {
    if (idx < 0 || idx >= MAX_WORKERS) return;
    worker_roles_[idx] = role;
    if (idx < num_workers_ && threads_[idx]) {
        // Apply immediately if thread is running.
        // SDL_SetThreadPriority sets priority of calling thread;
        // we can only hint — actual change requires thread cooperation.
        (void)role; // priority applied at thread spawn; runtime change not supported on all platforms
    }
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

        // called from within the worker thread itself via s_worker_entry).
        // We store the role; worker calls SDL_SetCurrentThreadPriority at startup.
        (void)worker_roles_[i];  // applied inside worker_loop()

#ifdef __linux__
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
    if (force_serial_) { fn(data); return; }
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

// Naughty Dog fiber pattern: submit with dependency counter.
// Counter is incremented here, decremented atomically when job completes.
// Caller can spin-wait on counter instead of blocking Flush().
void JobSystem::Submit(void (*fn)(void*), void* data, SDL_AtomicInt* counter) {
    if (force_serial_) {
        SDL_AtomicIncRef(counter);
        fn(data);
        SDL_AtomicDecRef(counter);
        return;
    }
    SDL_LockMutex(mtx_);
    while (count_ >= MAX_JOBS)
        SDL_WaitCondition(cv_cap_, mtx_);
    SDL_AtomicIncRef(counter);
    buf_[head_] = {fn, data, counter};
    head_ = (head_ + 1) % MAX_JOBS;
    ++count_;
    ++inflight_;
    SDL_BroadcastCondition(cv_work_);
    SDL_UnlockMutex(mtx_);
}
