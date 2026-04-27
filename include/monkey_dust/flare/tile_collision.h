#pragma once
#include <monkey_dust/flare/tile_map.h>

namespace md::flare {

// Build a NavMesh for NavSystem from a FlareMap's COLLISION layer.
//
// Walkable criterion: collision_layer_tile == 0.
// Non-zero collision tiles generate no NavMesh geometry → agents cannot
// traverse them. Pathfinding (NavSystem::QueryPath) works immediately after
// a successful call.
//
// tile_world_size: world-space footprint of one tile (must match the value
// passed to TileMapRenderer::Render so nav and visual stay in sync).
//
// Geometry layout matches TileMapRenderer's isometric diamond grid:
//   world_x = (col - row) * tile_world_size * 0.5
//   world_z = (col + row) * tile_world_size * 0.5
//
// Returns false when the collision layer is absent, the map is empty, or
// Recast/Detour fails to build the NavMesh.
bool BuildNavMeshFromTileMap(const FlareMap& map,
                             float tile_world_size = 1.0f);

// Returns the number of walkable tiles that would be submitted to NavMesh.
// Useful for stats/debugging without building.
int CountWalkableTiles(const FlareMap& map);

} // namespace md::flare
