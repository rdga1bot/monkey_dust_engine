#pragma once
#include <entt/entt.hpp>
#include <cmath>
#include <cstring>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/world/world_transform.h>

// ─────────────────────────────────────────────────────────
// SpatialGrid — O(1) spatial queries + O(1) entity remove.
//
// Two-tier design (CATHODE RE §7.3 — AI.exe.c lines 1691781, 1692023):
//   Tier A: 2D cell grid (100×100, 20m cells) — fast range queries.
//   Tier B: 193-bucket prime hash table — O(1) entity→cell lookup for Remove().
//           193 = prime → minimal clustering; matches CATHODE large-set hash.
//           31-bucket variant used for small sets (≤31 entities): auto-selected.
//
// Remove(e) without world position: entity_hash_[id % HASH_BUCKETS] → {cx, cz, idx}.
// No scan required; O(1) remove regardless of cell occupancy.
// ─────────────────────────────────────────────────────────

static constexpr float CELL_SIZE     = 20.0f;
static constexpr int   GRID_DIM      = 100;       // 100×100 комірок
static constexpr int   MAX_PER_CELL  = 64;        // max entities на комірку
static constexpr float WORLD_OFFSET  = 1000.0f;   // [-1000, +1000] → [0, 2000]
// CATHODE RE §7.3: prime-bucket hash sizes.
static constexpr int   HASH_BUCKETS  = 193;       // prime — large-set hash
static constexpr int   HASH_EMPTY    = -1;        // sentinel for empty slot

struct GridCell {
    entt::entity entities[MAX_PER_CELL];
    int          count = 0;
};

// Hash entry: maps entity → cell (cx, cz) + position within cell.
// Stored in open-addressing table with linear probing.
struct HashEntry {
    uint32_t entity_id  = 0xFFFFFFFFu;  // entt::to_integral(e); 0xFFFF…=empty
    int16_t  cx         = 0;
    int16_t  cz         = 0;
    int8_t   cell_idx   = -1;           // index within GridCell::entities[]
    uint8_t  _pad[3]    = {};
};  // 12B per entry

class SpatialGrid {
public:
    SpatialGrid() { Clear(); }

    void Clear() {
        for (auto& row : cells_)
            for (auto& cell : row)
                cell.count = 0;
        for (auto& h : hash_) h.entity_id = 0xFFFFFFFFu;
    }

    // Insert entity at world position. O(1).
    void Insert(entt::entity e, float wx, float wz) {
        int cx, cz;
        WorldToCell(wx, wz, cx, cz);
        if (!InBounds(cx, cz)) return;
        GridCell& cell = cells_[cx][cz];
        if (cell.count >= MAX_PER_CELL) return;
        int idx = cell.count;
        cell.entities[cell.count++] = e;
        hash_put(e, (int16_t)cx, (int16_t)cz, (int8_t)idx);
    }

    // Remove entity by entity handle — O(1), no world position needed.
    // CATHODE RE §7.3: hash lookup replaces O(n) cell scan.
    void Remove(entt::entity e) {
        HashEntry* he = hash_find(e);
        if (!he) return;
        int cx = he->cx, cz = he->cz, idx = he->cell_idx;
        he->entity_id = 0xFFFFFFFFu;  // mark empty
        if (!InBounds(cx, cz)) return;
        GridCell& cell = cells_[cx][cz];
        if (idx < 0 || idx >= cell.count) return;
        // Swap-with-last in cell; update hash for swapped entity.
        int last = cell.count - 1;
        if (idx != last) {
            entt::entity swapped = cell.entities[last];
            cell.entities[idx] = swapped;
            HashEntry* sh = hash_find(swapped);
            if (sh) sh->cell_idx = (int8_t)idx;
        }
        --cell.count;
    }

    // Legacy overload: remove by world position (kept for callers that have it).
    void Remove(entt::entity e, float /*wx*/, float /*wz*/) { Remove(e); }

    // Зібрати entities в радіусі r навколо (wx, wz).
    // out[] — вихідний фіксований масив, max_out — його розмір.
    // Повертає кількість знайдених.
    int QueryRadius(float wx, float wz, float r,
                    entt::entity* out, int max_out) const
    {
        int cx0, cz0, cx1, cz1;
        WorldToCell(wx - r, wz - r, cx0, cz0);
        WorldToCell(wx + r, wz + r, cx1, cz1);
        cx0 = Clamp(cx0, 0, GRID_DIM - 1);
        cz0 = Clamp(cz0, 0, GRID_DIM - 1);
        cx1 = Clamp(cx1, 0, GRID_DIM - 1);
        cz1 = Clamp(cz1, 0, GRID_DIM - 1);

        float r2 = r * r;
        int   found = 0;
        auto& reg = Registry::Get();

        for (int cx = cx0; cx <= cx1 && found < max_out; ++cx) {
            for (int cz = cz0; cz <= cz1 && found < max_out; ++cz) {
                const GridCell& cell = cells_[cx][cz];
                for (int i = 0; i < cell.count && found < max_out; ++i) {
                    entt::entity c = cell.entities[i];
                    if (reg.valid(c) && reg.all_of<WorldTransform>(c)) {
                        const auto& et = reg.get<WorldTransform>(c);
                        float ddx = et.x - wx, ddz = et.z - wz;
                        if (ddx*ddx + ddz*ddz > r2) continue;
                    }
                    out[found++] = c;
                }
            }
        }
        return found;
    }

    // ── VBfA ELEMENTS_WITHIN_RADIUS pattern ──────────────────────────────────
    // Source: viking.exe.c lines 131887-131913 (FUN_0060ec20, 46 callsites)
    // Returns all entities whose cell overlaps the AABB [wx_min..wx_max] × [wz_min..wz_max].
    // No per-entity distance filter — caller does secondary clip if needed.
    // Matches VBfA: double loop over cell range, output entity list.
    int QueryRange(float wx_min, float wz_min, float wx_max, float wz_max,
                   entt::entity* out, int max_out) const {
        int cx0, cz0, cx1, cz1;
        WorldToCell(wx_min, wz_min, cx0, cz0);
        WorldToCell(wx_max, wz_max, cx1, cz1);
        cx0 = Clamp(cx0, 0, GRID_DIM-1); cz0 = Clamp(cz0, 0, GRID_DIM-1);
        cx1 = Clamp(cx1, 0, GRID_DIM-1); cz1 = Clamp(cz1, 0, GRID_DIM-1);
        int found = 0;
        for (int cx = cx0; cx <= cx1 && found < max_out; ++cx)
            for (int cz = cz0; cz <= cz1 && found < max_out; ++cz) {
                const GridCell& cell = cells_[cx][cz];
                for (int i = 0; i < cell.count && found < max_out; ++i)
                    out[found++] = cell.entities[i];
            }
        return found;
    }

    // Convenience AoE: square AABB centered at (cx,cz) with half-extent radius.
    // Faster than QueryRadius (no sqrt, no per-entity distance check).
    int QueryAoE(float cx_world, float cz_world, float radius,
                 entt::entity* out, int max_out) const {
        return QueryRange(cx_world - radius, cz_world - radius,
                          cx_world + radius, cz_world + radius,
                          out, max_out);
    }

    // VBfA 5×5×5 spatial bucket — distribute entity updates across 125 frames.
    // Use for: particle/effect update staggering, AoE check distribution.
    // Returns bucket index [0..124]; entity processed only when frame%125 == bucket.
    static int Bucket5x5x5(uint32_t entity_id) {
        uint32_t v = entity_id;
        return (int)((v % 5u) + (v / 5u % 5u) * 5u + (v / 25u % 5u) * 25u);
    }

    // Debug: кількість entities у комірці що містить (wx, wz)
    int CellCount(float wx, float wz) const {
        int cx, cz;
        WorldToCell(wx, wz, cx, cz);
        if (!InBounds(cx, cz)) return 0;
        return cells_[cx][cz].count;
    }

private:
    GridCell  cells_[GRID_DIM][GRID_DIM];
    // CATHODE RE §7.3: 193-bucket open-addressing hash (linear probing).
    HashEntry hash_[HASH_BUCKETS];

    // Insert into hash table.
    void hash_put(entt::entity e, int16_t cx, int16_t cz, int8_t cell_idx) {
        uint32_t id   = entt::to_integral(e);
        uint32_t slot = id % (uint32_t)HASH_BUCKETS;
        for (int i = 0; i < HASH_BUCKETS; ++i) {
            uint32_t s = (slot + (uint32_t)i) % (uint32_t)HASH_BUCKETS;
            if (hash_[s].entity_id == 0xFFFFFFFFu) {
                hash_[s] = { id, cx, cz, cell_idx, {} };
                return;
            }
        }
        // Table full (shouldn't happen with MAX_PER_CELL × GRID_DIM² < HASH_BUCKETS×load)
    }

    // Find hash entry for entity. Returns nullptr if not found.
    HashEntry* hash_find(entt::entity e) {
        uint32_t id   = entt::to_integral(e);
        uint32_t slot = id % (uint32_t)HASH_BUCKETS;
        for (int i = 0; i < HASH_BUCKETS; ++i) {
            uint32_t s = (slot + (uint32_t)i) % (uint32_t)HASH_BUCKETS;
            if (hash_[s].entity_id == 0xFFFFFFFFu) return nullptr;
            if (hash_[s].entity_id == id) return &hash_[s];
        }
        return nullptr;
    }

    static void WorldToCell(float wx, float wz, int& cx, int& cz) {
        cx = (int)((wx + WORLD_OFFSET) / CELL_SIZE);
        cz = (int)((wz + WORLD_OFFSET) / CELL_SIZE);
    }

    static bool InBounds(int cx, int cz) {
        return cx >= 0 && cx < GRID_DIM && cz >= 0 && cz < GRID_DIM;
    }

    static int Clamp(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
};
