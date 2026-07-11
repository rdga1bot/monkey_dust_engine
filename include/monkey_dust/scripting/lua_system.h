#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <monkey_dust/ai/behavior_tree.h>
#include <entt/entt.hpp>

extern "C" {
#ifdef _WIN32
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#else
#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>
#endif
}

// LuaSystem — Lua 5.4 scripting host.
// Engine provides: Init/Shutdown, CallAction, RegisterFunction.
// Game registers its component-aware C functions via RegisterFunction().
// Sandbox guarantees:
//   - Instruction limit:   50 000 instructions/call (LUA_MASKCOUNT hook)
//   - Memory limit:        8 MB heap for the entire Lua state
//   - Blocked libraries:   io, os, package, debug, load, loadfile, dofile
static constexpr size_t LUA_MEM_LIMIT_BYTES = 8u * 1024u * 1024u; // 8 MB

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
    BTStatus CallAction(const char* func_name, MdEntity e);

    // Execute arbitrary Lua source; returns false on parse/runtime error. For tests only.
    bool Exec(const char* lua_code);

    bool IsReady() const { return L_ != nullptr; }

    size_t MemUsed() const { return lua_mem_used_; }

private:
    LuaSystem() = default;

    lua_State* L_          = nullptr;
    size_t     lua_mem_used_ = 0;

    static void   hook(lua_State* L, lua_Debug* ar);
    static int    md_log(lua_State* L);
    static void*  lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize);

    bool LoadFile(const char* path);
};
