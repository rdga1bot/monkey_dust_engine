#pragma once
// md_config.h — Runtime engine config reader.
//
// Port of VBfA pattern: data\tasks.txt / Tasks.txt parsed at startup to
// configure thread pool size, stack sizes, and other runtime params.
// All values have safe defaults so the file is optional.
//
// File format: data/md_config.txt
//   # comment
//   physics_threads   2
//   physics_temp_mb   8
//   logic_hz         10
//   max_bodies      1024
//
// Usage:
//   MdConfig cfg;
//   cfg.Load("data/md_config.txt");   // optional; missing file = defaults
//   int n = cfg.physics_threads;

#include <cstdio>
#include <cstring>

struct MdConfig {
    int   physics_threads  = 2;    // Jolt JobSystemThreadPool worker count
    int   physics_temp_mb  = 8;    // Jolt TempAllocatorImpl size in MB
    int   logic_hz         = 10;   // game logic tick rate
    int   max_bodies       = 1024; // Jolt max rigid bodies

    bool Load(const char* path = "data/md_config.txt") {
        FILE* f = fopen(path, "r");
        if (!f) return false;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            char key[64]; int val;
            if (sscanf(line, "%63s %d", key, &val) != 2) continue;
            if      (!strcmp(key, "physics_threads")) physics_threads = val;
            else if (!strcmp(key, "physics_temp_mb")) physics_temp_mb = val;
            else if (!strcmp(key, "logic_hz"))        logic_hz        = val;
            else if (!strcmp(key, "max_bodies"))      max_bodies      = val;
        }
        fclose(f);
        // Clamp to sane ranges.
        if (physics_threads < 1)  physics_threads = 1;
        if (physics_threads > 16) physics_threads = 16;
        if (physics_temp_mb < 4)  physics_temp_mb = 4;
        if (physics_temp_mb > 64) physics_temp_mb = 64;
        if (logic_hz < 1)         logic_hz = 1;
        if (logic_hz > 60)        logic_hz = 60;
        if (max_bodies < 64)      max_bodies = 64;
        if (max_bodies > 65536)   max_bodies = 65536;
        return true;
    }

    static MdConfig& Get() {
        static MdConfig inst;
        return inst;
    }
};
