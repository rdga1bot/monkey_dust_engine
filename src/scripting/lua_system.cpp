#include <monkey_dust/scripting/lua_system.h>
#include "raylib.h"
#include <cstring>
#include <cstdint>
#include <dirent.h>

void LuaSystem::hook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "[LuaSystem] instruction limit exceeded");
}

int LuaSystem::md_log(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (msg) TraceLog(LOG_INFO, "[Lua] %s", msg);
    return 0;
}

bool LuaSystem::LoadFile(const char* path) {
    if (luaL_dofile(L_, path) != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        TraceLog(LOG_WARNING, "[LuaSystem] Load error %s: %s",
                 path, err ? err : "?");
        lua_pop(L_, 1);
        return false;
    }
    TraceLog(LOG_INFO, "[LuaSystem] Loaded: %s", path);
    return true;
}

bool LuaSystem::Init(const char* scripts_dir) {
    L_ = luaL_newstate();
    if (!L_) return false;

    luaL_openlibs(L_);
    lua_register(L_, "md_log", md_log);
    lua_sethook(L_, hook, LUA_MASKCOUNT, 10000);

    if (!scripts_dir || scripts_dir[0] == '\0') return true;

    DIR* dir = opendir(scripts_dir);
    if (!dir) {
        TraceLog(LOG_WARNING, "[LuaSystem] Cannot open scripts dir: %s", scripts_dir);
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
    lua_sethook(L_, hook, LUA_MASKCOUNT, 10000);

    if (lua_pcall(L_, 1, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        TraceLog(LOG_WARNING, "[LuaSystem] Error in %s: %s",
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
