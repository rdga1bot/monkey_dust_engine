#pragma once
#include <monkey_dust/ai/behavior_tree.h>

struct BTComponent {
    BehaviorTree bt;
    char current_template[32] = {};
};
