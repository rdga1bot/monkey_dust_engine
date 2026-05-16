#pragma once
#include <cstdint>
#include <cstring>
#include <monkey_dust/ai/fnv.h>
#include <monkey_dust/platform/md_log.h>

// BTLuaScriptRegistry — maps fnv1a(script_name) → name string for ActionScript VM dispatch.
// Populated at BT JSON load time; queried by VM at tick time.
// Header-only singleton; MAX_SCRIPTS=16 fixed array, no heap.

static constexpr int BT_LUA_SCRIPT_MAX = 16;

struct BTLuaScriptEntry {
    uint32_t hash;
    char     name[48];
};

class BTLuaScriptRegistry {
public:
    static BTLuaScriptRegistry& Get() {
        static BTLuaScriptRegistry s_inst;
        return s_inst;
    }

    // Register script name; returns its fnv1a hash.
    uint32_t Register(const char* name) {
        if (!name || !name[0]) return 0u;
        uint32_t h = md::fnv1a(name);
        for (int i = 0; i < count_; ++i) {
            if (entries_[i].hash == h) return h;
        }
        if (count_ >= BT_LUA_SCRIPT_MAX) {
            MD_LOG(MD_LOG_WARNING, "[BTLuaScriptRegistry] table full, dropping '%s'", name);
            return h;
        }
        entries_[count_].hash = h;
        strncpy(entries_[count_].name, name, sizeof(entries_[count_].name) - 1);
        entries_[count_].name[sizeof(entries_[count_].name) - 1] = '\0';
        ++count_;
        return h;
    }

    // Returns script name for given hash, or nullptr if not registered.
    const char* Lookup(uint32_t hash) const {
        for (int i = 0; i < count_; ++i) {
            if (entries_[i].hash == hash) return entries_[i].name;
        }
        return nullptr;
    }

    void Clear() { count_ = 0; }

private:
    BTLuaScriptEntry entries_[BT_LUA_SCRIPT_MAX]{};
    int count_{0};
};
