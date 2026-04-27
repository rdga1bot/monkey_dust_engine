#include <monkey_dust/ai/bt_action_registry.h>
#include <cstring>

namespace md {

BTActionRegistry& BTActionRegistry::Get() {
    static BTActionRegistry inst;
    return inst;
}

bool BTActionRegistry::RegisterAction(const char* name, BTActionFunc fn) {
    if (action_count_ >= MAX_ACTIONS) return false;
    strncpy(action_names_[action_count_], name, NAME_LEN - 1);
    action_names_[action_count_][NAME_LEN - 1] = '\0';
    action_fns_[action_count_] = fn;
    ++action_count_;
    return true;
}

bool BTActionRegistry::RegisterCondition(const char* name, BTConditionFunc fn) {
    if (cond_count_ >= MAX_CONDITIONS) return false;
    strncpy(cond_names_[cond_count_], name, NAME_LEN - 1);
    cond_names_[cond_count_][NAME_LEN - 1] = '\0';
    cond_fns_[cond_count_] = fn;
    ++cond_count_;
    return true;
}

bool BTActionRegistry::RegisterBuilder(const char* name, BuilderFn fn) {
    if (builder_count_ >= MAX_BUILDERS) return false;
    strncpy(builder_names_[builder_count_], name, NAME_LEN - 1);
    builder_names_[builder_count_][NAME_LEN - 1] = '\0';
    builder_fns_[builder_count_] = fn;
    ++builder_count_;
    return true;
}

BTActionFunc BTActionRegistry::FindAction(const char* name) const {
    for (int i = 0; i < action_count_; ++i)
        if (strncmp(action_names_[i], name, NAME_LEN) == 0)
            return action_fns_[i];
    return nullptr;
}

BTConditionFunc BTActionRegistry::FindCondition(const char* name) const {
    for (int i = 0; i < cond_count_; ++i)
        if (strncmp(cond_names_[i], name, NAME_LEN) == 0)
            return cond_fns_[i];
    return nullptr;
}

BTActionRegistry::BuilderFn BTActionRegistry::FindBuilder(const char* name) const {
    for (int i = 0; i < builder_count_; ++i)
        if (strncmp(builder_names_[i], name, NAME_LEN) == 0)
            return builder_fns_[i];
    return nullptr;
}

void BTActionRegistry::Clear() {
    action_count_  = 0;
    cond_count_    = 0;
    builder_count_ = 0;
}

} // namespace md
