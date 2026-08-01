#include <monkey_dust/world/terrain_tile.h>
#include <monkey_dust/world/world_registry.h>
#include <algorithm>

namespace {
int ClampZone(int g) {
    return std::max(0, std::min(TerrainTileGrid::kGridN - 1, g));
}
} // namespace

TerrainTile TerrainTileGrid::ForZone(int gx, int gz) {
    gx = ClampZone(gx);
    gz = ClampZone(gz);

    TerrainTile t;
    t.gx = gx;
    t.gz = gz;
    t.origin_x = (float)gx * CHUNK_SIZE;
    t.origin_z = (float)gz * CHUNK_SIZE;
    t.extent   = CHUNK_SIZE;
    t.layers   = &BiomeRegistry::Get().ForZone(WorldRegistry::Get().GetBiomeAt(gx, gz));
    return t;
}

TerrainTile TerrainTileGrid::ForWorldPos(float wx, float wz) {
    int gx = (int)(wx / CHUNK_SIZE);
    int gz = (int)(wz / CHUNK_SIZE);
    return ForZone(gx, gz);
}
