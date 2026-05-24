#pragma once
// equipment.h — Per-item instance data and worn equipment slots.
//
// ItemInstance: one concrete item with grade + condition (not a stack).
// EquipmentComponent: 7 worn slots matching ItemLimbSlot enum from item_def.h.
//
// Kenshi RE: each piece of armour/weapon has individual grade and condition.
// Condition degrades on damage; repair raises it back (requires materials).

#include <monkey_dust/world/item_def.h>
#include <monkey_dust/world/shop_inventory.h>  // ItemGrade
#include <cstring>

// ── ItemInstance — single concrete item with per-instance state ───────────────

struct ItemInstance {
    uint32_t  item_def_id = 0;       // index into ItemDatabase
    uint16_t  condition   = 1000;    // 0..1000 (0=broken, 1000=perfect)
    uint8_t   grade       = (uint8_t)ItemGrade::Standard;
    uint8_t   _pad        = 0;

    bool IsEmpty()   const noexcept { return item_def_id == 0; }
    bool IsBroken()  const noexcept { return condition == 0; }
    float ConditionF() const noexcept { return (float)condition / 1000.f; }

    // Damage reduces condition; clamped to 0.
    void Damage(uint16_t amount) noexcept {
        condition = (condition > amount) ? (uint16_t)(condition - amount) : 0u;
    }

    // Repair raises condition toward 1000.
    void Repair(uint16_t amount) noexcept {
        uint32_t c = (uint32_t)condition + amount;
        condition = (c < 1000u) ? (uint16_t)c : 1000u;
    }

    void Clear() noexcept { item_def_id = 0; condition = 1000; grade = (uint8_t)ItemGrade::Standard; _pad = 0; }
};
static_assert(sizeof(ItemInstance) == 8, "ItemInstance must be 8 bytes");

// ── EquipmentComponent — 7 worn slots (one per limb + weapon hand) ────────────
// Slot indices match ItemLimbSlot enum:
//   0=None(unused) 1=Head 2=Torso 3=LArm 4=RArm 5=LLeg 6=RLeg
// Weapon is stored at slot 0 (re-purposed from None → MainHand).

static constexpr int EQUIP_SLOT_COUNT = 7;

struct EquipmentComponent {
    ItemInstance slots[EQUIP_SLOT_COUNT];  // 7 × 8B = 56B
    uint8_t      _pad[8] = {};             //             8B → total 64B

    // Equip an item into the appropriate limb slot (derived from ItemDef.limb_slot).
    // Returns false if slot already occupied or item has no slot.
    bool Equip(const ItemInstance& item, const ItemDef* def) noexcept {
        if (!def) return false;
        int s = (int)def->limb_slot;
        if (s < 0 || s >= EQUIP_SLOT_COUNT) return false;
        if (!slots[s].IsEmpty()) return false;
        slots[s] = item;
        return true;
    }

    // Unequip slot; returns the removed instance (caller should add to Inventory).
    ItemInstance Unequip(int slot_idx) noexcept {
        if (slot_idx < 0 || slot_idx >= EQUIP_SLOT_COUNT) return {};
        ItemInstance out = slots[slot_idx];
        slots[slot_idx].Clear();
        return out;
    }

    bool IsSlotEmpty(int slot_idx) const noexcept {
        if (slot_idx < 0 || slot_idx >= EQUIP_SLOT_COUNT) return true;
        return slots[slot_idx].IsEmpty();
    }

    // Effective cut resistance: sum of all worn armour pieces × condition factor.
    float TotalCutResist(const ItemDef* defs[EQUIP_SLOT_COUNT]) const noexcept {
        float total = 0.f;
        for (int i = 1; i < EQUIP_SLOT_COUNT; ++i) {
            if (slots[i].IsEmpty() || !defs[i]) continue;
            total += defs[i]->cut_resist * slots[i].ConditionF();
        }
        return total;
    }

    void Clear() noexcept {
        for (int i = 0; i < EQUIP_SLOT_COUNT; ++i) slots[i].Clear();
        memset(_pad, 0, sizeof(_pad));
    }
};
static_assert(sizeof(EquipmentComponent) == 64, "EquipmentComponent must be 64 bytes");
