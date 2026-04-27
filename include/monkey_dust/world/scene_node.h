#pragma once
#include <cstdint>

struct SceneNode {
    float    x, y, z;
    float    scale;
    uint32_t type_id;
    bool     valid;
};

static constexpr int MAX_SCENE_NODES = 512;

struct SceneTree {
    SceneNode nodes[MAX_SCENE_NODES];
    int       count = 0;

    int Add(float x, float y, float z, float scale, uint32_t type_id) {
        if (count >= MAX_SCENE_NODES) return -1;
        nodes[count] = {x, y, z, scale, type_id, true};
        return count++;
    }

    void Remove(int idx) {
        if (idx < 0 || idx >= count) return;
        nodes[idx] = nodes[--count];
        nodes[count].valid = false;
    }

    void Clear() {
        for (int i = 0; i < count; ++i) nodes[i].valid = false;
        count = 0;
    }
};
