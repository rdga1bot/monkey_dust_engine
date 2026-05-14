#pragma once
#include <monkey_dust/ai/behavior_tree.h>

// ── BTJsonLoader ──────────────────────────────────────────────────────────────
// Loads JSON behavior trees into BehaviorTree objects.
// Parser: custom strstr-based, no external JSON libraries (project rule).
// Actions/conditions resolved via md::BTActionRegistry::Get() at load time.
//
// JSON format (mirror of AI BEHAVIOR XML, adapted to JSON):
//   {
//     "name": "systematic_search",
//     "root": {
//       "type": "SelectorLinear",
//       "children": [
//         {
//           "type": "DecoratorBranch",
//           "branch_type": "Dead",
//           "condition": "condIsDead",
//           "children": [{ "type": "ActionNode", "action": "actDead" }]
//         },
//         {
//           "type": "SequenceLinear",
//           "children": [
//             { "type": "MotivationCheck", "motivation": "Search" },
//             { "type": "TimerStart", "timer_slot": "SearchTimeout", "duration_ms": 30000 },
//             { "type": "ActionNode", "action": "actSystematicSearch" }
//           ]
//         }
//       ]
//     }
//   }
//
// Supported node types:
//   SelectorLinear, SequenceLinear, SequenceStateless
//   DecoratorBranch  (fields: branch_type, condition, shutdown_speed)
//   ConditionNode    (fields: condition)
//   ActionNode       (fields: action)
//   ReferencedBehavior (fields: tree — resolved to nullptr on first load)
//   TimerStart       (fields: timer_slot, duration_ms, only_increase)
//   TimerCheck       (fields: timer_slot)
//   MotivationCheck, SetMotivation (fields: motivation)
//   FlagCheck, FlagSet             (fields: flag, check_set|set)
//   FrameFlagCheck, FrameFlagSet   (fields: flag, check_set|set)
//   SenseCheck       (fields: sense[0|1], threshold)
//   GaugeCheck, GaugeSet           (fields: gauge, threshold|value)
//   WeightedSelector (fields: weights[4])
//   AwarenessCheck   (fields: state)
//   AlertnessCheck   (fields: state)
//   MoodCheck        (fields: mood)
//   RoleCheck        (fields: role, mode["performing"|"could_perform"])
//   RoleClaim        (fields: role, query_id)
//   RoleRelease      (fields: role)
//   WithdrawCheck, SetWithdraw (fields: state)
//   Inverter
//   Repeat           (fields: count — 0=infinite)
//   Wait             (fields: duration_ms)
class BTJsonLoader {
public:
    // Load from file. Returns true on success.
    static bool LoadFromFile(BehaviorTree& bt, const char* path);

    // Load from null-terminated JSON string. Returns true on success.
    // Useful for tests and embedded data.
    static bool LoadFromString(BehaviorTree& bt, const char* json);

    // Read the tree name without fully parsing it.
    // Writes up to name_max-1 chars to out_name. Returns false if not found.
    static bool ReadName(const char* json, char* out_name, int name_max);
};
