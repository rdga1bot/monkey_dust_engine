#pragma once
#include <cstdint>
#include <cmath>
#include <entt/entt.hpp>

static constexpr float CHUNK_SIZE            = 500.0f;  // Kenshi zone size
static constexpr int   CHUNK_LOAD_RADIUS     = 3;
static constexpr int   MAX_CHUNKS_ACTIVE     = (CHUNK_LOAD_RADIUS * 2 + 1)
                                              * (CHUNK_LOAD_RADIUS * 2 + 1); // 49
static constexpr int   MAX_ENTITIES_PER_CHUNK = 256;

struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
    bool operator!=(const ChunkCoord& o) const { return !(*this == o); }
};

struct ChunkData {
    ChunkCoord   coord;
    entt::entity entities[MAX_ENTITIES_PER_CHUNK];
    int          entity_count;
    bool         loaded;
    bool         dirty;
    char         filename[64];
};

inline ChunkCoord WorldToChunk(float wx, float wz) {
    return { (int)floorf(wx / CHUNK_SIZE), (int)floorf(wz / CHUNK_SIZE) };
}

inline int ChunkDist(ChunkCoord a, ChunkCoord b) {
    int dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}
