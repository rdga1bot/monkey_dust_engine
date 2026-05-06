#pragma once
#include <monkey_dust/building/production_chain.h>
#include <cstdint>

struct Building {
    uint32_t        def_id;
    int             grid_x, grid_z;
    int             size_x, size_z;
    ProductionChain chain;
    float           progress_s;
    bool            active;
};
