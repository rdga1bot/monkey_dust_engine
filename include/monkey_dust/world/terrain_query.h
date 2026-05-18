#pragma once
// TerrainQuery — single source of truth for terrain spatial data.
//
// Every system that needs to know where the ground is uses this.
// Wraps the active chunk grid; must be Init()ed after terrain chunks are loaded.
//
// Rule: Y positions for ALL entities and props must come from here, never
// from TerrainChunk::SampleHeight() called ad-hoc in scattered places.

#include <monkey_dust/world/terrain_chunk.h>
#include <monkey_dust/world/chunk_def.h>
#include <cmath>

class TerrainQuery {
public:
    static TerrainQuery& Get() {
        static TerrainQuery s;
        return s;
    }

    // Call once after terrain chunks are loaded.
    void Init(TerrainChunk* chunks,  // [tnkn][tnkn] row-major
              int    tnkn,           // chunks per side
              float  chunk_size,
              float  world_off_x,
              float  world_off_z) {
        chunks_      = chunks;
        tnkn_        = tnkn;
        chunk_size_  = chunk_size;
        world_off_x_ = world_off_x;
        world_off_z_ = world_off_z;
        ready_       = true;
    }

    bool IsReady() const { return ready_; }

    // Ground height at world (wx, wz).  Returns 0 if outside loaded terrain.
    float GetHeight(float wx, float wz) const {
        if (!ready_) return 0.f;
        float gx = wx - world_off_x_;
        float gz = wz - world_off_z_;
        int   cx = (int)(gx / chunk_size_);
        int   cz = (int)(gz / chunk_size_);
        if (cx < 0 || cx >= tnkn_ || cz < 0 || cz >= tnkn_) return 0.f;
        const TerrainChunk& chunk = chunks_[cz * tnkn_ + cx];
        if (!chunk.loaded) return 0.f;
        float lx = gx - cx * chunk_size_;
        float lz = gz - cz * chunk_size_;
        return chunk.SampleHeight(lx, lz);
    }

    // Surface normal at world (wx, wz) — central difference over 0.5m.
    // Returns (0,1,0) if outside loaded terrain.
    void GetNormal(float wx, float wz,
                   float& nx, float& ny, float& nz) const {
        static constexpr float D = 0.5f;
        float hL = GetHeight(wx - D, wz);
        float hR = GetHeight(wx + D, wz);
        float hB = GetHeight(wx, wz - D);
        float hF = GetHeight(wx, wz + D);
        // Cross product of tangent vectors
        float tx = 2.f * D, ty = hR - hL, tz = 0.f;
        float bx = 0.f,     by = hF - hB, bz = 2.f * D;
        nx = ty * bz - tz * by;
        ny = tz * bx - tx * bz;
        nz = tx * by - ty * bx;
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
        else             { nx = 0.f; ny = 1.f; nz = 0.f; }
    }

    // Slope angle in radians (0 = flat, π/2 = vertical cliff).
    float GetSlope(float wx, float wz) const {
        float nx, ny, nz;
        GetNormal(wx, wz, nx, ny, nz);
        return acosf(ny < 1.f ? ny : 1.f);
    }

private:
    TerrainQuery() = default;
    TerrainChunk* chunks_      = nullptr;
    int           tnkn_        = 0;
    float         chunk_size_  = 64.f;
    float         world_off_x_ = 0.f;
    float         world_off_z_ = 0.f;
    bool          ready_       = false;
};
