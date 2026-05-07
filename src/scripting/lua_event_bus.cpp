#include <monkey_dust/scripting/lua_event_bus.h>
#include <monkey_dust/scripting/lua_system.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

LuaEventBus& LuaEventBus::Get() {
    static LuaEventBus inst;
    return inst;
}

void LuaEventBus::Register(const char* event_name, const char* func_name) {
    if (!event_name || !func_name) return;
    if (count_ >= MAX_HANDLERS) {
        MD_LOG(MD_LOG_WARNING, "[LuaEventBus] handler limit (%d) reached", MAX_HANDLERS);
        return;
    }
    strncpy(handlers_[count_].event, event_name, sizeof(handlers_[0].event) - 1);
    strncpy(handlers_[count_].func,  func_name,  sizeof(handlers_[0].func)  - 1);
    ++count_;
    MD_LOG(MD_LOG_INFO, "[LuaEventBus] %s → %s", event_name, func_name);
}

void LuaEventBus::Fire(const char* event_name, entt::entity entity_id) {
    if (!event_name) return;
    for (int i = 0; i < count_; ++i) {
        if (strcmp(handlers_[i].event, event_name) == 0)
            LuaSystem::Get().CallAction(handlers_[i].func, entity_id);
    }
}

void LuaEventBus::Clear() {
    memset(handlers_, 0, sizeof(handlers_));
    count_ = 0;
}
