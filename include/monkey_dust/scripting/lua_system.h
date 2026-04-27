#pragma once
#include <monkey_dust/ai/behavior_tree.h>
#include <entt/entt.hpp>

extern "C" {
#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>
}

// LuaSystem — Lua 5.4 scripting host.
// Engine provides: Init/Shutdown, CallAction, RegisterFunction.
// Game registers its component-aware C functions via RegisterFunction().
// Instruction-limit hook prevents infinite loops (LUA_MASKCOUNT=10000).
class LuaSystem {
public:
    static LuaSystem& Get() {
        static LuaSystem inst;
        return inst;
    }

    bool Init(const char* scripts_dir);
    void Shutdown();

    // Let game register game-specific C API functions
    void RegisterFunction(const char* name, lua_CFunction fn);

    // Calls named Lua function(entity_id) → BTStatus
    BTStatus CallAction(const char* func_name, entt::entity e);

    bool IsReady() const { return L_ != nullptr; }

private:
    LuaSystem() = default;

    lua_State* L_ = nullptr;

    static void hook(lua_State* L, lua_Debug* ar);
    static int  md_log(lua_State* L);  // engine-level: TraceLog only

    bool LoadFile(const char* path);
};
