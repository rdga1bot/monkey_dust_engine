#pragma once
#include <cstdint>
#include <entt/entt.hpp>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/platform/md_log.h>

// StateHandlerRegistry — MD pattern: each EntityStateFlag bit change
// dispatches to a registered callback instead of a generic set_flag(bool).
// MAX_HANDLERS slots; no heap allocation; raw function pointers only.
//
// Usage:
//   StateHandlerRegistry::Get().Register(EntityStateFlag::Frozen, on_freeze);
//   StateHandlerRegistry::Get().Apply(entity, EntityStateFlag::Frozen, true);

struct StateHandlerContext {
    entt::entity entity;
    EntityStateFlag flag;
    bool set;      // true = flag being set; false = flag being cleared
};

using StateHandlerFn = void (*)(StateHandlerContext);

class StateHandlerRegistry {
public:
    static constexpr uint8_t MAX_HANDLERS = 16;

    static StateHandlerRegistry& Get() noexcept {
        static StateHandlerRegistry inst;
        return inst;
    }

    // Register a handler for a specific flag. Returns slot index or -1 on full.
    int8_t Register(EntityStateFlag flag, StateHandlerFn fn) noexcept {
        if (count_ >= MAX_HANDLERS) {
            MD_LOG(MD_LOG_WARNING, "StateHandlerRegistry: full (%d slots)", MAX_HANDLERS);
            return -1;
        }
        entries_[count_] = { flag, fn };
        return (int8_t)count_++;
    }

    // Unregister all handlers for a flag.
    void Unregister(EntityStateFlag flag) noexcept {
        uint8_t w = 0;
        for (uint8_t r = 0; r < count_; ++r) {
            if (entries_[r].flag != flag) entries_[w++] = entries_[r];
        }
        count_ = w;
    }

    void Clear() noexcept { count_ = 0; }

    // Apply flag change to entity's EntityStateFlag bitmask + dispatch handlers.
    void Apply(entt::entity e, EntityStateFlag flag, bool set,
               entt::registry& reg) const noexcept {
        auto* as = reg.try_get<AgentState>(e);
        if (!as) return;

        uint32_t bit = static_cast<uint32_t>(flag);
        if (set) as->entity_state |=  bit;
        else     as->entity_state &= ~bit;

        StateHandlerContext ctx{ e, flag, set };
        for (uint8_t i = 0; i < count_; ++i) {
            if (entries_[i].flag == flag) entries_[i].fn(ctx);
        }
    }

    // Test: does the entity have the flag set?
    static bool Test(entt::entity e, EntityStateFlag flag,
                     const entt::registry& reg) noexcept {
        const auto* as = reg.try_get<AgentState>(e);
        if (!as) return false;
        return (as->entity_state & static_cast<uint32_t>(flag)) != 0u;
    }

    uint8_t count() const noexcept { return count_; }

private:
    struct Entry { EntityStateFlag flag; StateHandlerFn fn; };
    Entry   entries_[MAX_HANDLERS] = {};
    uint8_t count_ = 0;
};
