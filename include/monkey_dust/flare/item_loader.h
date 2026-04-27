#pragma once
#include <cstdint>

namespace md::flare {

constexpr int MAX_ITEMS = 2048;

enum class FlareItemType : uint8_t {
    NONE, MELEE, RANGED, ARMOR, ACCESSORY, POTION, CURRENCY, QUEST, OTHER
};

struct ItemDef {
    int           id;
    char          name[64];
    FlareItemType type;
    int           icon;
    int           price, price_sell, max_quantity;
    int           abs_min, abs_max;
    int           dmg_melee_min, dmg_melee_max;
    int           dmg_ment_min,  dmg_ment_max;
    int           dmg_ranged_min, dmg_ranged_max;
    bool          valid;
};

struct ItemRegistry {
    ItemDef defs[MAX_ITEMS];
    int     count;
};

// Parse <mod_chain[0]>/items/items.txt following INCLUDE directives (≤ 3 deep).
// INCLUDE paths are searched across the full mod chain (child → base).
// Returns false when items.txt cannot be opened in chain[0].
bool LoadItems(const char* const* mod_chain, int n, ItemRegistry& out);

inline bool LoadItems(const char* mod_path, ItemRegistry& out) {
    return LoadItems(&mod_path, 1, out);
}

// Linear search by id.
const ItemDef* FindItem(const ItemRegistry& reg, int id);

} // namespace md::flare
