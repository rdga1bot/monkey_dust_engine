#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <entt/entt.hpp>

// LuaEventBus — maps named C++ events to registered Lua callback functions.
// Handlers are registered from Lua via md_on_event() or from C++ via Register().
// Multiple handlers per event name are supported (all are called on Fire).
class LuaEventBus {
public:
    static LuaEventBus& Get();

    // Register a global Lua function as handler for event_name.
    void Register(const char* event_name, const char* func_name);

    // Call all handlers registered for event_name(entity_id).
    // Pass entt::null for non-entity events (quest, dialog, etc.).
    void Fire(const char* event_name, MdEntity entity_id = entt::null);

    void Clear();

private:
    LuaEventBus() = default;

    static constexpr int MAX_HANDLERS = 64;
    struct Handler { char event[32]; char func[32]; };
    Handler handlers_[MAX_HANDLERS] = {};
    int     count_ = 0;
};
