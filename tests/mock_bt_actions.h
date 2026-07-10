#pragma once
#include <monkey_dust/ai/behavior_tree.h>
#include <monkey_dust/ai/bt_action_registry.h>

namespace md::test {

// Mock BT actions/conditions for engine VM unit tests.
// NOT for production — engine/tests/ only.
// Counters are global; call MockReset() before each test.

inline int  g_mock_tick_count = 0;
inline bool g_mock_cond_value = true;

inline BTStatus mock_act_success(md::EngineContext&, MdEntity) {
    ++g_mock_tick_count; return BTStatus::Success;
}
inline BTStatus mock_act_failure(md::EngineContext&, MdEntity) {
    ++g_mock_tick_count; return BTStatus::Failure;
}
inline BTStatus mock_act_running(md::EngineContext&, MdEntity) {
    ++g_mock_tick_count; return BTStatus::Running;
}
inline bool mock_cond_true(md::EngineContext&, MdEntity) {
    return g_mock_cond_value;
}

inline void MockReset() { g_mock_tick_count = 0; g_mock_cond_value = true; }

inline void RegisterMockActions(md::BTActionRegistry& r) {
    r.RegisterAction("mock_success", mock_act_success);
    r.RegisterAction("mock_failure", mock_act_failure);
    r.RegisterAction("mock_running", mock_act_running);
    r.RegisterCondition("mock_true",  mock_cond_true);
}

} // namespace md::test
