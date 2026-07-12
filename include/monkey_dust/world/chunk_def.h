#pragma once
#include <cstdint>
#include <cmath>
#include <monkey_dust/ecs/md_entity.h>

// Kenshi zone size. Ogre Terrain::setTerrainScale() gives horizontal_scale=294912.0
// world UNITS (re_docs/kenshi/terrain.md), but Kenshi's engine unit is 1 unit = 0.1m
// (decimetre), not 1m — confirmed by cross-checking against the real map size
// (29.491km x 29.491km, measured from save-file coordinates by the Kenshi community;
// 294912/10 = 29491.2m matches to 5 sig figs). Real zone size = 29491.2/64 = 460.8m.
// (A prior version of this fix used 4608.0f, treating the raw units as metres
// directly — 10x too large; corrected after the world looked wrong at that scale.)
static constexpr float CHUNK_SIZE            = 460.8f;
static constexpr int   CHUNK_LOAD_RADIUS     = 3;
static constexpr int   MAX_CHUNKS_ACTIVE     = (CHUNK_LOAD_RADIUS * 2 + 1)
                                              * (CHUNK_LOAD_RADIUS * 2 + 1); // 49
static constexpr int   MAX_ENTITIES_PER_CHUNK = 2048;

struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
    bool operator!=(const ChunkCoord& o) const { return !(*this == o); }
};

// ── ChunkStreamState ──────────────────────────────────────────────────────────
// 4-stage lifecycle confirmed by CATHODE Zone Manager RE (AI.exe):
//   add_to_lo → LOAD → add_to_al → ACTIVE → add_to_su → STREAM → add_to_un → UNLOAD
// STREAM = graceful wind-down: entities saved, physics removed, but geometry still
// rendered to avoid pop-out while player moves away. ~1-2 Update() ticks.
enum class ChunkStreamState : uint8_t {
    Inactive = 0,  // slot empty
    Loading  = 1,  // IO worker reading from disk (add_to_lo)
    Active   = 2,  // fully loaded, entities spawned, physics active (add_to_al)
    Streaming= 3,  // queued for removal: save in progress, entities despawning (add_to_su)
    Unloading= 4,  // final teardown: geometry removed (add_to_un)
};

struct ChunkData {
    ChunkCoord       coord;
    MdEntity     entities[MAX_ENTITIES_PER_CHUNK];
    int              entity_count;
    ChunkStreamState stream_state;  // replaces bool loaded (backward compat below)
    bool             dirty;
    char             filename[64];

    bool loaded() const noexcept { return stream_state == ChunkStreamState::Active; }
};

inline ChunkCoord WorldToChunk(float wx, float wz) {
    return { (int)floorf(wx / CHUNK_SIZE), (int)floorf(wz / CHUNK_SIZE) };
}

inline int ChunkDist(ChunkCoord a, ChunkCoord b) {
    int dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}
