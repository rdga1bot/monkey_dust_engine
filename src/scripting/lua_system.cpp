#include <monkey_dust/scripting/lua_system.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>
#include <cstdint>
#include <monkey_dust/compat/md_dirent.h>

// Custom allocator: enforce LUA_MEM_LIMIT_BYTES.
// Returns nullptr when limit exceeded — Lua handles OOM gracefully.
void* LuaSystem::lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* used = static_cast<size_t*>(ud);
    if (nsize == 0) {
        *used -= osize;
        free(ptr);
        return nullptr;
    }
    if (*used - osize + nsize > LUA_MEM_LIMIT_BYTES)
        return nullptr; // allocation refused → Lua raises MemError
    void* p = realloc(ptr, nsize);
    if (p) *used = *used - osize + nsize;
    return p;
}

void LuaSystem::hook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "[LuaSystem] instruction limit exceeded");
}

int LuaSystem::md_log(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (msg) MD_LOG(MD_LOG_INFO, "[Lua] %s", msg);
    return 0;
}

bool LuaSystem::LoadFile(const char* path) {
    if (luaL_dofile(L_, path) != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        MD_LOG(MD_LOG_WARNING, "[LuaSystem] Load error %s: %s",
                 path, err ? err : "?");
        lua_pop(L_, 1);
        return false;
    }
    MD_LOG(MD_LOG_INFO, "[LuaSystem] Loaded: %s", path);
    return true;
}

bool LuaSystem::Init(const char* scripts_dir) {
    lua_mem_used_ = 0;
    L_ = lua_newstate(lua_alloc, &lua_mem_used_);
    if (!L_) return false;

    luaL_openlibs(L_);

    // Remove dangerous standard libraries — sandbox enforcement.
    // luaL_openlibs opens io/os/package/debug which allow shell exec and FS access.
    static const char* const blocked[] = {
        "io", "os", "package", "debug", nullptr
    };
    for (int i = 0; blocked[i]; ++i) {
        lua_pushnil(L_);
        lua_setglobal(L_, blocked[i]);
    }
    // Also block the load* family (can compile arbitrary bytecode)
    static const char* const blocked_fns[] = {
        "dofile", "loadfile", "load", "loadstring", nullptr
    };
    for (int i = 0; blocked_fns[i]; ++i) {
        lua_pushnil(L_);
        lua_setglobal(L_, blocked_fns[i]);
    }

    lua_register(L_, "md_log", md_log);
    lua_sethook(L_, hook, LUA_MASKCOUNT, 50000);

    if (!scripts_dir || scripts_dir[0] == '\0') return true;

    DIR* dir = opendir(scripts_dir);
    if (!dir) {
        MD_LOG(MD_LOG_WARNING, "[LuaSystem] Cannot open scripts dir: %s", scripts_dir);
        return true;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".lua") == 0) {
            char path_buf[256];
            snprintf(path_buf, sizeof(path_buf), "%s/%s", scripts_dir, name);
            LoadFile(path_buf);
        }
    }
    closedir(dir);
    return true;
}

void LuaSystem::RegisterFunction(const char* name, lua_CFunction fn) {
    if (L_) lua_register(L_, name, fn);
}

void LuaSystem::Shutdown() {
    if (L_) { lua_close(L_); L_ = nullptr; }
}

BTStatus LuaSystem::CallAction(const char* func_name, entt::entity e) {
    if (!L_) return BTStatus::Failure;

    lua_getglobal(L_, func_name);
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        return BTStatus::Failure;
    }

    lua_pushinteger(L_, static_cast<lua_Integer>(static_cast<uint32_t>(e)));
    lua_sethook(L_, hook, LUA_MASKCOUNT, 50000);

    if (lua_pcall(L_, 1, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        MD_LOG(MD_LOG_WARNING, "[LuaSystem] Error in %s: %s",
                 func_name, err ? err : "?");
        lua_pop(L_, 1);
        return BTStatus::Failure;
    }

    BTStatus result = BTStatus::Running;
    if (lua_isstring(L_, -1)) {
        const char* s = lua_tostring(L_, -1);
        if (strncmp(s, "success", 7) == 0)      result = BTStatus::Success;
        else if (strncmp(s, "failure", 7) == 0) result = BTStatus::Failure;
    }
    lua_pop(L_, 1);
    return result;
}

bool LuaSystem::Exec(const char* lua_code) {
    if (!L_ || !lua_code) return false;
    lua_sethook(L_, hook, LUA_MASKCOUNT, 50000);
    if (luaL_dostring(L_, lua_code) != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        MD_LOG(MD_LOG_WARNING, "[LuaSystem] Exec error: %s", err ? err : "?");
        lua_pop(L_, 1);
        return false;
    }
    return true;
}
