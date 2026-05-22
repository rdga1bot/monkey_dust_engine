#pragma once
// ZoneActivityMap — persistent bitfield tracking per-zone visit/dirty state.
// Kenshi pattern: unvisited zones use static data only; visited zones have
// serialized entity state. This map is a lightweight index (separate from the
// full world save) so the game knows which chunks need full deserialization.
//
// Layout: flat binary file "saves/zone_activity.bin"
//   uint32 magic  (0x5A4F4E45 = "ZONE")
//   uint32 version (1)
//   uint8  flags[GRID * GRID]  — one byte per zone, bitfield below
//
// Bit flags per zone:
//   bit 0 = VISITED  — player has entered this zone at least once
//   bit 1 = DIRTY    — zone has entity state changes not yet serialized
//   bit 2 = ACTIVE   — zone is currently streamed in (runtime-only, not persisted)

#include <cstdint>
#include <cstdio>
#include <cstring>

static constexpr int ZONE_GRID = 64;  // 64×64 Kenshi world grid
static constexpr int ZONE_TOTAL = ZONE_GRID * ZONE_GRID;

namespace ZoneFlag {
    constexpr uint8_t VISITED = 1u << 0;
    constexpr uint8_t DIRTY   = 1u << 1;
    constexpr uint8_t ACTIVE  = 1u << 2;  // runtime only
}

class ZoneActivityMap {
public:
    static ZoneActivityMap& Get() noexcept {
        static ZoneActivityMap inst;
        return inst;
    }

    void Reset() noexcept { memset(flags_, 0, sizeof(flags_)); }

    uint8_t GetFlags(int gx, int gz) const noexcept {
        if (gx < 0 || gx >= ZONE_GRID || gz < 0 || gz >= ZONE_GRID) return 0;
        return flags_[gz * ZONE_GRID + gx];
    }

    void SetFlag(int gx, int gz, uint8_t mask) noexcept {
        if (gx < 0 || gx >= ZONE_GRID || gz < 0 || gz >= ZONE_GRID) return;
        flags_[gz * ZONE_GRID + gx] |= mask;
    }

    void ClearFlag(int gx, int gz, uint8_t mask) noexcept {
        if (gx < 0 || gx >= ZONE_GRID || gz < 0 || gz >= ZONE_GRID) return;
        flags_[gz * ZONE_GRID + gx] &= (uint8_t)~mask;
    }

    bool IsVisited(int gx, int gz) const noexcept {
        return (GetFlags(gx, gz) & ZoneFlag::VISITED) != 0;
    }
    bool IsDirty  (int gx, int gz) const noexcept {
        return (GetFlags(gx, gz) & ZoneFlag::DIRTY  ) != 0;
    }

    // Mark zone visited + dirty when player enters.
    void OnPlayerEnter(int gx, int gz) noexcept {
        SetFlag(gx, gz, ZoneFlag::VISITED | ZoneFlag::ACTIVE);
    }

    // Call when entity state is serialized for a zone — clears dirty flag.
    void OnZoneSaved(int gx, int gz) noexcept {
        ClearFlag(gx, gz, ZoneFlag::DIRTY);
    }

    // Collect all dirty zone coords into out[], returns count (max = max_out).
    int GetDirtyZones(int* gx_out, int* gz_out, int max_out) const noexcept {
        int n = 0;
        for (int z = 0; z < ZONE_GRID && n < max_out; ++z)
            for (int x = 0; x < ZONE_GRID && n < max_out; ++x)
                if (flags_[z * ZONE_GRID + x] & ZoneFlag::DIRTY) {
                    gx_out[n] = x;
                    gz_out[n] = z;
                    ++n;
                }
        return n;
    }

    bool Save(const char* path) const noexcept {
        FILE* f = fopen(path, "wb");
        if (!f) return false;
        uint32_t hdr[2] = { 0x5A4F4E45u, 1u };  // "ZONE", version=1
        fwrite(hdr, sizeof(hdr), 1, f);
        // Clear ACTIVE bit before persisting (runtime-only)
        uint8_t tmp[ZONE_TOTAL];
        for (int i = 0; i < ZONE_TOTAL; ++i)
            tmp[i] = flags_[i] & (uint8_t)~ZoneFlag::ACTIVE;
        fwrite(tmp, 1, ZONE_TOTAL, f);
        fclose(f);
        return true;
    }

    bool Load(const char* path) noexcept {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        uint32_t hdr[2] = {};
        fread(hdr, sizeof(hdr), 1, f);
        if (hdr[0] != 0x5A4F4E45u || hdr[1] != 1u) { fclose(f); return false; }
        fread(flags_, 1, ZONE_TOTAL, f);
        fclose(f);
        // ACTIVE bit is runtime-only: clear on load
        for (int i = 0; i < ZONE_TOTAL; ++i)
            flags_[i] &= (uint8_t)~ZoneFlag::ACTIVE;
        return true;
    }

private:
    ZoneActivityMap() { memset(flags_, 0, sizeof(flags_)); }
    uint8_t flags_[ZONE_TOTAL] = {};
};
