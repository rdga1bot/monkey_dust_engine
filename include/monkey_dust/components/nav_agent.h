#pragma once
#include <monkey_dust/nav/path_cache.h>

struct NavAgent {
    float target_x, target_z;
    float path[MAX_PATH_LEN * 3];
    int   path_len;
    int   path_idx;
};
