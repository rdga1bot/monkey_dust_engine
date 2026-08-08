#pragma once

// Kenshi zone size. Ogre Terrain::setTerrainScale() gives horizontal_scale=294912.0
// world UNITS (re_docs/kenshi/terrain.md), but Kenshi's engine unit is 1 unit = 0.1m
// (decimetre), not 1m — confirmed by cross-checking against the real map size
// (29.491km x 29.491km, measured from save-file coordinates by the Kenshi community;
// 294912/10 = 29491.2m matches to 5 sig figs). Real zone size = 29491.2/64 = 460.8m.
// (A prior version of this fix used 4608.0f, treating the raw units as metres
// directly — 10x too large; corrected after the world looked wrong at that scale.)
static constexpr float CHUNK_SIZE            = 460.8f;

struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
    bool operator!=(const ChunkCoord& o) const { return !(*this == o); }
};
