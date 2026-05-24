#pragma once
#include <cstdint>
#include <cstring>

static constexpr int INV_MAX_SLOTS = 64;

struct Inventory {
    uint32_t item_ids[INV_MAX_SLOTS];
    int      amounts [INV_MAX_SLOTS];
    float    weights [INV_MAX_SLOTS];  // kg per item_id (set on Add from ItemDatabase)
    int      slot_count;
    float    total_weight;             // sum of weights[i]*amounts[i]
    float    max_carry_weight;         // cap; 0 = unlimited

    void Clear() {
        memset(item_ids, 0, sizeof(item_ids));
        memset(amounts,  0, sizeof(amounts));
        memset(weights,  0, sizeof(weights));
        slot_count    = 0;
        total_weight  = 0.f;
    }

    bool Add(uint32_t item_id, int amount, float item_weight = 0.f) {
        for (int i = 0; i < slot_count; ++i) {
            if (item_ids[i] == item_id) {
                amounts[i] += amount;
                total_weight += item_weight * (float)amount;
                return true;
            }
        }
        if (slot_count >= INV_MAX_SLOTS) return false;
        item_ids[slot_count] = item_id;
        amounts [slot_count] = amount;
        weights [slot_count] = item_weight;
        total_weight += item_weight * (float)amount;
        slot_count++;
        return true;
    }

    bool Take(uint32_t item_id, int amount) {
        for (int i = 0; i < slot_count; ++i) {
            if (item_ids[i] != item_id) continue;
            if (amounts[i] < amount) return false;
            total_weight -= weights[i] * (float)amount;
            amounts[i] -= amount;
            if (amounts[i] == 0) {
                item_ids[i] = item_ids[slot_count - 1];
                amounts [i] = amounts [slot_count - 1];
                weights [i] = weights [slot_count - 1];
                slot_count--;
            }
            return true;
        }
        return false;
    }

    bool IsOverloaded() const { return max_carry_weight > 0.f && total_weight > max_carry_weight; }

    int Count(uint32_t item_id) const {
        for (int i = 0; i < slot_count; ++i)
            if (item_ids[i] == item_id) return amounts[i];
        return 0;
    }
};
