#include <monkey_dust/combat/power_slot_manager.h>
#include <monkey_dust/combat/power_system.h>
#include <monkey_dust/combat/power_def.h>
#include <monkey_dust/combat/power_manager.h>

namespace md {

PowerSlotManager::EntitySlots* PowerSlotManager::Find(MdEntity e) {
    for (int i = 0; i < count_; ++i)
        if (entries_[i].entity == e) return &entries_[i];
    return nullptr;
}

const PowerSlotManager::EntitySlots* PowerSlotManager::Find(MdEntity e) const {
    for (int i = 0; i < count_; ++i)
        if (entries_[i].entity == e) return &entries_[i];
    return nullptr;
}

PowerSlotManager::EntitySlots* PowerSlotManager::FindOrCreate(MdEntity e) {
    if (auto* s = Find(e)) return s;
    if (count_ >= MAX_TRACKED) return nullptr;
    entries_[count_].entity = e;
    auto* s = &entries_[count_++];
    for (int i = 0; i < NUM_SLOTS; ++i) { s->power_ids[i] = -1; s->last_use_s[i] = -9999.0f; }
    return s;
}

void PowerSlotManager::Assign(MdEntity e, int slot, int power_id) {
    if (slot < 0 || slot >= NUM_SLOTS) return;
    auto* s = FindOrCreate(e);
    if (s) s->power_ids[slot] = power_id;
}

int PowerSlotManager::GetSlot(MdEntity e, int slot) const {
    if (slot < 0 || slot >= NUM_SLOTS) return -1;
    const auto* s = Find(e);
    return s ? s->power_ids[slot] : -1;
}

float PowerSlotManager::CooldownRemaining(MdEntity e, int slot, float now_s) const {
    if (slot < 0 || slot >= NUM_SLOTS) return 0.0f;
    const auto* s = Find(e);
    if (!s || s->power_ids[slot] < 0) return 0.0f;
    const PowerDef* def = PowerManager::Get().Find(s->power_ids[slot]);
    if (!def) return 0.0f;
    float cooldown_s = def->cooldown_ms * 0.001f;
    float elapsed = now_s - s->last_use_s[slot];
    float remaining = cooldown_s - elapsed;
    return remaining > 0.0f ? remaining : 0.0f;
}

bool PowerSlotManager::Use(MdEntity e, int slot, float tx, float tz, float now_s) {
    if (slot < 0 || slot >= NUM_SLOTS) return false;
    auto* s = FindOrCreate(e);
    if (!s) return false;
    int power_id = s->power_ids[slot];
    if (power_id < 0) return false;
    if (CooldownRemaining(e, slot, now_s) > 0.0f) return false;
    if (!PowerSystem::Get().Use(e, power_id, tx, tz)) return false;
    s->last_use_s[slot] = now_s;
    return true;
}

void PowerSlotManager::Clear() {
    for (int i = 0; i < count_; ++i) {
        entries_[i] = EntitySlots{};
        entries_[i].entity = entt::null;
    }
    count_ = 0;
}

} // namespace md
