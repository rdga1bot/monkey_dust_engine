#pragma once
#include <cstdint>

// LuaScriptComponent — marks entity as having an associated Lua behavior script.
// script_id = fnv1a(script_name); 0 = inactive/unassigned.
// Checked by BTNodeType::ConditionHasScript; executed by BTNodeType::ActionScript.
struct LuaScriptComponent {
    uint32_t script_id;
};
