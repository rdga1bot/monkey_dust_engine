#pragma once
#include <entt/entt.hpp>
#include <cmath>
#include <cstring>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/world/world_transform.h>

// ─────────────────────────────────────────────────────────
// SpatialGrid — O(1) пошук сусідніх entities.
//
// Світ: MAX_WORLD×MAX_WORLD метрів, комірки CELL_SIZE×CELL_SIZE.
// 1000×1000м / 20м = 50×50 = 2500 комірок.
// Кожна комірка — фіксований масив MAX_PER_CELL entities.
//
// Без цього: O(N²) = 250,000 перевірок/тік при 500 NPC.
// З ним:     O(1)  = ~9 комірок на запит.
// ─────────────────────────────────────────────────────────

static constexpr float CELL_SIZE     = 20.0f;
static constexpr int   GRID_DIM      = 100;      // 100×100 комірок
static constexpr int   MAX_PER_CELL  = 64;       // max entities на комірку
static constexpr float WORLD_OFFSET  = 1000.0f;  // [-1000, +1000] → [0, 2000]

struct GridCell {
    entt::entity entities[MAX_PER_CELL];
    int          count = 0;
};

class SpatialGrid {
public:
    SpatialGrid() { Clear(); }

    void Clear() {
        for (auto& row : cells_)
            for (auto& cell : row)
                cell.count = 0;
    }

    // Додати entity у комірку за world-координатами.
    // Викликати при спавні або переміщенні > CELL_SIZE/2.
    void Insert(entt::entity e, float wx, float wz) {
        int cx, cz;
        WorldToCell(wx, wz, cx, cz);
        if (!InBounds(cx, cz)) return;
        GridCell& cell = cells_[cx][cz];
        if (cell.count < MAX_PER_CELL)
            cell.entities[cell.count++] = e;
    }

    // Видалити entity з комірки (при переміщенні або смерті).
    void Remove(entt::entity e, float wx, float wz) {
        int cx, cz;
        WorldToCell(wx, wz, cx, cz);
        if (!InBounds(cx, cz)) return;
        GridCell& cell = cells_[cx][cz];
        for (int i = 0; i < cell.count; ++i) {
            if (cell.entities[i] == e) {
                cell.entities[i] = cell.entities[--cell.count];
                return;
            }
        }
    }

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

    // Debug: кількість entities у комірці що містить (wx, wz)
    int CellCount(float wx, float wz) const {
        int cx, cz;
        WorldToCell(wx, wz, cx, cz);
        if (!InBounds(cx, cz)) return 0;
        return cells_[cx][cz].count;
    }

private:
    GridCell cells_[GRID_DIM][GRID_DIM];

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
