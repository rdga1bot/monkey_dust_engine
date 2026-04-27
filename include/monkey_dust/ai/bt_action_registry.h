#pragma once
#include <monkey_dust/ai/behavior_tree.h>
#include <cstdint>

namespace md {

// BT action/condition/builder registry.
// Game registers its functions in main.cpp BEFORE BTLoader::LoadFromFile().
// Load-time lookup only (FindAction called once per JSON node) — not runtime strcmp.
// No std::vector/map/string (§2.2 constraint). O(N) lookup, N≤64.
class BTActionRegistry {
public:
    static constexpr int MAX_ACTIONS    = 64;
    static constexpr int MAX_CONDITIONS = 64;
    static constexpr int MAX_BUILDERS   = 32;
    static constexpr int NAME_LEN       = 32;

    using BuilderFn = void(*)(BehaviorTree&);

    bool RegisterAction   (const char* name, BTActionFunc fn);
    bool RegisterCondition(const char* name, BTConditionFunc fn);
    bool RegisterBuilder  (const char* name, BuilderFn fn);

    BTActionFunc    FindAction   (const char* name) const;
    BTConditionFunc FindCondition(const char* name) const;
    BuilderFn       FindBuilder  (const char* name) const;

    void Clear();  // for tests and hot-reload

    static BTActionRegistry& Get();

private:
    char            action_names_[MAX_ACTIONS][NAME_LEN]{};
    BTActionFunc    action_fns_[MAX_ACTIONS]{};
    int             action_count_ = 0;

    char            cond_names_[MAX_CONDITIONS][NAME_LEN]{};
    BTConditionFunc cond_fns_[MAX_CONDITIONS]{};
    int             cond_count_ = 0;

    char            builder_names_[MAX_BUILDERS][NAME_LEN]{};
    BuilderFn       builder_fns_[MAX_BUILDERS]{};
    int             builder_count_ = 0;
};

} // namespace md
