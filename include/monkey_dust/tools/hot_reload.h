#pragma once
#include <cstdint>

// ── HotReload ─────────────────────────────────────────────────────────────────
// File-modification watcher for *.json data files.
// Background thread polls mtime every poll_ms milliseconds.
// Calls registered ReloadFunc on the WATCHER thread — callers must be
// thread-safe (typically just queues a flag read by the game loop).
//
// Usage:
//   HotReload::Get().Watch("data/ai/director_profiles.json", [](const char* p){
//       DirectorSystem::Get().Init(p);
//   });
//   HotReload::Get().Start(500);  // poll every 500 ms
//   // later:
//   HotReload::Get().Stop();
//
// Limits: MAX_WATCHED=16 fixed entries (BSS), no malloc in poll loop.
// Platform: POSIX stat(2). No-op on unsupported platforms.

using ReloadFunc = void(*)(const char* path);

class HotReload {
public:
    static HotReload& Get() { static HotReload i; return i; }

    // Register a file to watch. Ignores duplicates (same path).
    // Returns false if already at MAX_WATCHED capacity.
    bool Watch(const char* path, ReloadFunc fn);

    // Remove a previously registered path.
    void Unwatch(const char* path);

    // Start background polling thread. poll_ms: interval in milliseconds.
    // Safe to call multiple times — stops old thread first.
    void Start(uint32_t poll_ms = 500);

    // Stop background thread (blocks until it exits).
    void Stop();

    // Force a synchronous check of all watched files (no thread needed).
    // Useful for single-threaded tests.
    void PollOnce();

    static constexpr int MAX_WATCHED = 16;

private:
    HotReload() = default;

    struct WatchEntry {
        char       path[256] = {};
        ReloadFunc fn        = nullptr;
        int64_t    last_mtime = -1;  // -1 = uninitialized
    };

    WatchEntry entries_[MAX_WATCHED] = {};
    int        count_  = 0;
    uint32_t   poll_ms_= 500;

    // Thread state
    volatile bool running_ = false;
    void*         thread_  = nullptr;  // SDL_Thread*

    void Poll();
    static int ThreadFunc(void* userdata);
};
