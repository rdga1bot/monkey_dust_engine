#pragma once
#include <cstdint>
#include <cstring>

static constexpr int INV_MAX_SLOTS = 64;

struct Inventory {
    uint32_t item_ids[INV_MAX_SLOTS];
    int      amounts [INV_MAX_SLOTS];
    int      slot_count;

    void Clear() {
        memset(item_ids, 0, sizeof(item_ids));
        memset(amounts,  0, sizeof(amounts));
        slot_count = 0;
    }

    bool Add(uint32_t item_id, int amount) {
        for (int i = 0; i < slot_count; ++i) {
            if (item_ids[i] == item_id) { amounts[i] += amount; return true; }
        }
        if (slot_count >= INV_MAX_SLOTS) return false;
        item_ids[slot_count] = item_id;
        amounts [slot_count] = amount;
        slot_count++;
        return true;
    }

    bool Take(uint32_t item_id, int amount) {
        for (int i = 0; i < slot_count; ++i) {
            if (item_ids[i] != item_id) continue;
            if (amounts[i] < amount) return false;
            amounts[i] -= amount;
            if (amounts[i] == 0) {
                item_ids[i] = item_ids[slot_count - 1];
                amounts [i] = amounts [slot_count - 1];
                slot_count--;
            }
            return true;
        }
        return false;
    }

    int Count(uint32_t item_id) const {
        for (int i = 0; i < slot_count; ++i)
            if (item_ids[i] == item_id) return amounts[i];
        return 0;
    }
};
