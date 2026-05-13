#pragma once
#include <cstdint>
#include <future>
#include <cstring>

// Engine-level save system: pure async atomic file writer.
// Game registers a WriteCallback that serializes gameplay data;
// SaveSystem calls it and wraps the result in .tmp → rename atomics.
//
// POD record structs (NpcRecord, BuildingRecord, etc.) and SAVE_VERSION
// live in game/src/world/world_serializer.h — not here.

using SaveWriteCallback = bool(*)(const char* path, void* userdata);
using SaveReadCallback  = bool(*)(const char* path, void* userdata);

class SaveSystem {
public:
    static SaveSystem& Get() {
        static SaveSystem inst;
        return inst;
    }

    // Game registers callbacks before first Save/Load
    void SetCallbacks(SaveWriteCallback write_cb, SaveReadCallback read_cb,
                      void* userdata) {
        write_cb_ = write_cb;
        read_cb_  = read_cb;
        userdata_ = userdata;
    }

    // Synchronous save. Returns true on success.
    bool SaveSync(const char* path);

    // Async save (F5). Call CheckAsyncDone() each frame for result.
    void SaveAsync(const char* path);
    bool IsAsyncPending() const { return async_pending_; }
    bool CheckAsyncDone();

    // Load. Returns true on success.
    bool Load(const char* path);

    static const char* DefaultPath() { return "saves/autosave.mdsave"; }

private:
    SaveSystem() = default;

    SaveWriteCallback  write_cb_      = nullptr;
    SaveReadCallback   read_cb_       = nullptr;
    void*              userdata_      = nullptr;
    std::future<bool>  async_future_;
    char               async_path_[256] = {};
    bool               async_pending_ = false;
    bool               last_async_ok_ = false;
};
