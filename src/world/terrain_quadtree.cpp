#include <monkey_dust/world/terrain_quadtree.h>

void TerrainQuadtree::Init(float world_size_m, int max_depth) {
    root_.origin_x = -world_size_m * 0.5f;
    root_.origin_z = -world_size_m * 0.5f;
    root_.size     = world_size_m;
    root_.depth    = 0;
    max_depth_     = max_depth;
}
