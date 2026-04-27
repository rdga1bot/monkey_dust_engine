#include <monkey_dust/save/save_system.h>
#include <cstdio>
#include <string>
#include <chrono>

bool SaveSystem::SaveSync(const char* path) {
    if (!write_cb_) return false;
    return write_cb_(path, userdata_);
}

void SaveSystem::SaveAsync(const char* path) {
    if (async_pending_) return;

    std::string path_str = path;
    async_pending_ = true;
    async_future_ = std::async(std::launch::async,
        [this, path_str]() mutable -> bool {
            return write_cb_ ? write_cb_(path_str.c_str(), userdata_) : false;
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
