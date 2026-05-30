#pragma once
// ShopInventory — Kenshi-style vendor stock with grade, restock, and price calc.
// Attach as ECS component to vendor NPCs or settlement building entities.
//
// PriceCalc:
//   sell_price = base_value × grade_mult × relation_mult × supply_mult
//   buy_price  = sell_price × BUY_BACK_MULT (player sells to shop cheaper)
//
// Restock: call Restock() when restock_timer_s reaches 0.
//   Timer is in game-seconds (1 game-day = 1440 real-seconds at monkey_dust scale).

#include <monkey_dust/world/item_def.h>
#include <cstring>

// ── Item grade (quality tier) ─────────────────────────────────────────────────

enum class ItemGrade : uint8_t {
    Rusted     = 0,
    Worn       = 1,
    Standard   = 2,
    High       = 3,
    Specialist = 4,
    Masterwork = 5,
    Meitou     = 6,   // unique / named — player cannot craft
};

static constexpr float ITEM_GRADE_MULT[7] = {
    0.50f,  // Rusted
    0.65f,  // Worn
    1.00f,  // Standard
    1.50f,  // High
    1.75f,  // Specialist
    2.00f,  // Masterwork
    3.00f,  // Meitou
};

// ── ShopSlot ─────────────────────────────────────────────────────────────────

static constexpr int MAX_SHOP_SLOTS = 32;

struct ShopSlot {
    uint32_t  item_def_id = 0;     // ItemDatabase index
    uint8_t   quantity    = 0;     // current stock (0 = slot empty)
    uint8_t   grade       = (uint8_t)ItemGrade::Standard;
    uint8_t   _pad[2]     = {};
};
static_assert(sizeof(ShopSlot) == 8, "ShopSlot must be 8 bytes");

// ── ShopInventory ECS component ───────────────────────────────────────────────

// 1 game-day = 60 * 24 = 1440 real-seconds (game_time_hours ticks at 1h/60s).
static constexpr float SHOP_RESTOCK_INTERVAL_S = 1440.f * 3.f;  // every 3 game-days

struct ShopInventory {
    ShopSlot  slots[MAX_SHOP_SLOTS];
    uint32_t  faction_id         = 0;
    float     restock_timer_s    = SHOP_RESTOCK_INTERVAL_S;
    int       slot_count         = 0;   // active slots (0..MAX_SHOP_SLOTS)
    uint8_t   buys_illegal       = 0;   // 1 = accepts stolen/illegal goods
    uint8_t   buys_stolen        = 0;
    uint8_t   _pad[2]            = {};

    // Find a slot by item_def_id; returns -1 if not in stock.
    int FindSlot(uint32_t def_id) const noexcept {
        for (int i = 0; i < slot_count; ++i)
            if (slots[i].item_def_id == def_id) return i;
        return -1;
    }

    // Add a new stock entry (or accumulate quantity on existing slot).
    void AddStock(uint32_t def_id, uint8_t qty,
                  ItemGrade grade = ItemGrade::Standard) noexcept {
        int idx = FindSlot(def_id);
        if (idx >= 0) {
            uint32_t total = (uint32_t)slots[idx].quantity + qty;
            slots[idx].quantity = (uint8_t)(total < 255u ? total : 255u);
            return;
        }
        if (slot_count >= MAX_SHOP_SLOTS) return;
        slots[slot_count].item_def_id = def_id;
        slots[slot_count].quantity    = qty;
        slots[slot_count].grade       = (uint8_t)grade;
        ++slot_count;
    }

    // Remove qty from stock; returns actual amount removed.
    int TakeStock(uint32_t def_id, int qty) noexcept {
        int idx = FindSlot(def_id);
        if (idx < 0 || slots[idx].quantity == 0) return 0;
        int taken = (slots[idx].quantity < qty) ? slots[idx].quantity : qty;
        slots[idx].quantity -= (uint8_t)taken;
        return taken;
    }

    // Advance restock timer; call with real delta_s from logic tick.
    // Returns true if a restock fired this tick (caller may log/animate).
    bool TickRestock(float delta_s) noexcept {
        restock_timer_s -= delta_s;
        if (restock_timer_s > 0.f) return false;
        Restock();
        restock_timer_s = SHOP_RESTOCK_INTERVAL_S;
        return true;
    }

    // Refill each slot back to its default quantity (quantity=0 → 5 standard).
    void Restock() noexcept {
        for (int i = 0; i < slot_count; ++i) {
            if (slots[i].quantity < 5)
                slots[i].quantity = 5;
        }
    }
};

// ── PriceConfig ───────────────────────────────────────────────────────────────
// Kenshi RE: 14 per-category price multipliers (kenshi_x64.exe.c offsets [0x28, 0x32..0x3e]).
// global_price_mult is the base; all others are multiplied by it at load.
// Load from game config; access via GetPriceConfig().
struct PriceConfig {
    float global_price_mult         = 1.00f; // [0x37] base multiplier (applied to all below)
    float food_price_mult           = 1.00f; // [0x28]
    float crossbow_price_mult       = 1.00f; // [0x32]
    float robotics_price_mult       = 1.20f; // [0x33]
    float armor_price_mult          = 1.00f; // [0x34]
    float sword_price_mult          = 1.00f; // [0x35]
    float trade_price_mult          = 1.00f; // [0x36]
    float clothing_price_mult       = 0.80f; // [0x38]
    float trade_profit_margins      = 0.30f; // [0x39]
    float loot_price_mult_gear      = 0.60f; // [0x3a]
    float loot_price_mult           = 0.50f; // [0x3b]
    float loot_price_mult_pl_armour = 0.55f; // [0x3c]
    float loot_price_mult_pl_weapon = 0.55f; // [0x3d]
    float blueprint_price_mult      = 2.00f; // [0x3e]
};
static_assert(sizeof(PriceConfig) == 56, "PriceConfig layout");

static inline PriceConfig& GetPriceConfig() noexcept {
    static PriceConfig cfg;
    return cfg;
}

// ── PriceCalc ─────────────────────────────────────────────────────────────────
// Kenshi formula: final_price = base_value × category_mult × grade_mult × relation_mult × supply_mult
// relation: int8_t [-100..100] from FactionSystem::GetRelation
// buy_value clamped to [1000, 20000] (Kenshi RE: aiStack constants).

namespace PriceCalc {

static constexpr float BUY_VALUE_MIN  = 1000.f;  // absolute sell floor (cats)
static constexpr float BUY_VALUE_MAX  = 20000.f; // sell ceiling
static constexpr float BUY_BACK_MULT  = 0.40f;   // player sells to shop at 40% of sell price
static constexpr float SUPPLY_LOW_MULT  = 1.30f;
static constexpr float SUPPLY_HIGH_MULT = 0.80f;
static constexpr int   SUPPLY_LOW_THRESHOLD  = 5;   // qty < 5 → low supply
static constexpr int   SUPPLY_HIGH_THRESHOLD = 20;  // qty > 20 → surplus

inline float RelationMult(int8_t relation, bool player_is_buying) noexcept {
    // Higher relation → cheaper for buyer, more expensive for seller.
    float r = (float)relation;
    if (player_is_buying) {
        // Good relation → discount: 1.0 - rel * 0.003  (max 30% off at +100)
        return (r >= 0.f) ? 1.f - r * 0.003f
                          : 1.f + (-r) * 0.005f;  // hostile: +50% surcharge
    } else {
        // Selling to shop: inverse (shop pays less when unfriendly)
        return (r >= 0.f) ? 1.f + r * 0.002f      // bonus for friend
                          : 1.f - (-r) * 0.003f;  // penalty for enemy
    }
}

inline float SupplyMult(int qty) noexcept {
    if (qty < SUPPLY_LOW_THRESHOLD)  return SUPPLY_LOW_MULT;
    if (qty > SUPPLY_HIGH_THRESHOLD) return SUPPLY_HIGH_MULT;
    return 1.f;
}

// Clamp a computed sell value to Kenshi [1000, 20000] range.
inline float BuyValueClamp(float v) noexcept {
    if (v < BUY_VALUE_MIN) return BUY_VALUE_MIN;
    if (v > BUY_VALUE_MAX) return BUY_VALUE_MAX;
    return v;
}

// Price player pays when buying from shop.
// category_mult: pass GetPriceConfig().armor_price_mult (or similar) for item type.
inline float SellPrice(float base_value, ItemGrade grade,
                       int8_t relation, int qty_in_stock,
                       float category_mult = 1.f) noexcept {
    int gi = (int)grade;
    if (gi < 0 || gi > 6) gi = (int)ItemGrade::Standard;
    float p = base_value
            * category_mult
            * GetPriceConfig().global_price_mult
            * ITEM_GRADE_MULT[gi]
            * RelationMult(relation, /*player_is_buying=*/true)
            * SupplyMult(qty_in_stock);
    return (p < 1.f) ? 1.f : p;
}

// Price shop pays when player sells to it (clamped to Kenshi range).
inline float BuyPrice(float sell_price) noexcept {
    float p = BuyValueClamp(sell_price) * BUY_BACK_MULT;
    return (p < 1.f) ? 1.f : p;
}

}  // namespace PriceCalc
