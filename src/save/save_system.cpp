#include <monkey_dust/save/save_system.h>
#include <cstdio>
#include <cstring>
#include <chrono>

bool SaveSystem::SaveSync(const char* path) {
    if (!write_cb_) return false;
    return write_cb_(path, userdata_);
}

void SaveSystem::SaveAsync(const char* path) {
    if (async_pending_) return;

    strncpy(async_path_, path, sizeof(async_path_) - 1);
    async_path_[sizeof(async_path_) - 1] = '\0';
    async_pending_ = true;
    async_future_ = std::async(std::launch::async,
        [this]() -> bool {
            return write_cb_ ? write_cb_(async_path_, userdata_) : false;
        });
}

bool SaveSystem::CheckAsyncDone() {
    if (!async_pending_) return false;
    if (async_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;

    last_async_ok_ = async_future_.get();
    async_pending_ = false;
    return true;
}

bool SaveSystem::Load(const char* path) {
    if (!read_cb_) return false;
    return read_cb_(path, userdata_);
}
