#pragma once
#include <monkey_dust/nav/path_cache.h>

struct NavAgent {
    float    target_x, target_z;
    float    path[MAX_PATH_LEN * 3];
    int      path_len;
    int      path_idx;
    bool     is_moving     = false;  // set by actMoveToTarget, read by animator
    float    move_speed    = 0.0f;   // 0=still, 1=walk, 2=run (for blend)
    float    walk_speed    = 3.5f;   // m/s — walk animation threshold
    float    run_speed     = 7.0f;   // m/s — run animation threshold; used by chase/flee
    int      crowd_idx     = -1;     // dtCrowd agent index; -1 = not in crowd
    float    desired_vel_x = 0.0f;   // set by CrowdSystem, read by JoltWorld
    float    desired_vel_z = 0.0f;
};
