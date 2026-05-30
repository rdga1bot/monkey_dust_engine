#pragma once
#include <monkey_dust/nav/path_cache.h>

// alignas(16): VBfA RE §8 — hot path, SSE loads on target_x/z and desired_vel_x/z.
struct alignas(16) NavAgent {
    float    target_x, target_z;
    float    path[MAX_PATH_LEN * 3];
    int      path_len;
    int      path_idx;
    bool     is_moving     = false;  // set by actMoveToTarget, read by animator
    float    move_speed    = 0.0f;   // 0=still, 1=walk, 2=run (for blend)
    float    walk_speed    = 1.7f;   // m/s — Kenshi walk ~5.5 km/h (was 3.5)
    float    run_speed     = 4.5f;   // m/s — Kenshi run ~16 km/h (was 7.0)
    int      crowd_idx     = -1;     // dtCrowd agent index; -1 = not in crowd
    float    desired_vel_x = 0.0f;   // set by CrowdSystem, read by JoltWorld + render lerp
    float    desired_vel_z = 0.0f;
    float    render_x      = 0.0f;   // render-rate extrapolated position (smoothing)
    float    render_z      = 0.0f;
};
