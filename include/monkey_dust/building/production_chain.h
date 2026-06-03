#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────
// ProductionChain — data-driven виробнича ланцюжок.
//
// Завантажується з buildings.json.
// Кожен будинок з production != null має один ланцюжок:
//   inputs[]  → витрачаємо при старті циклу
//   outputs[] → додаємо до Inventory після time_s секунд
//
// MAX_SLOTS = 4 (більше не потрібно для MVP)
// ─────────────────────────────────────────────────────────

static constexpr int PROD_MAX_SLOTS = 4;

struct ItemStack {
    uint32_t item_id;   // 0 = empty slot
    int      amount;
};

// A-2: QUEUED = cycle complete but more queued_cycles remain.
enum class ProductionState : uint8_t { IDLE = 0, RUNNING = 1, FULL = 2, NO_INPUT = 3, QUEUED = 4 };

struct ProductionChain {
    ItemStack inputs [PROD_MAX_SLOTS];
    ItemStack outputs[PROD_MAX_SLOTS];
    float     time_s;              // тривалість одного циклу (секунди)
    int       input_count;
    int       output_count;
    int       storage_capacity;    // 0 = необмежено; max accumulated output items
    int       max_output_per_cycle;// сума outputs[].amount (обчислюється при завантаженні)
    ProductionState state;
    bool      valid;               // false = будівля без виробництва

    // A-2: Kenshi Permanent_Job / CraftingQueue repeat logic.
    // repeat_job=true + queued_cycles=0 → unlimited repeat (Permanent_Job).
    // repeat_job=true + queued_cycles>0 → run N more cycles, then IDLE.
    bool    repeat_job    = false; // auto-restart after each cycle completion
    uint8_t queued_cycles = 0;     // cycles remaining (0 = unlimited when repeat_job=true)
    uint8_t cycles_done   = 0;     // total cycles completed (saturates at 255)
    uint8_t _pad          = 0;
};
