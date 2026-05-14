#pragma once
#include <monkey_dust/components/npc_memory.h>
#include <cstdint>

// ── Batch 6: SuspiciousItem Group system ──────────────────────────────────────
// Coordinates multiple NPCs investigating the same suspicious item.
// Game code: call FindOrCreate()+Join() when assigning NPCs to an investigation.
// BT code: conditions read group state via SuspiciousItemGroupComponent::group_id.
//
// Invariants:
//   - group_id == INVALID_GROUP (0xFF) → Failure on all group conditions
//   - IsFirstMember: members[0] == entity_raw
//   - AllRoutingTo: routing_count == member_count (and member_count > 0)
//   - WaitingForRouting: routing_count < member_count (at least one not yet routing)
//   - progress_allowed: explicit gate set by game code / BT ActionAllowGroupProgress

static constexpr uint8_t MAX_SUSPICIOUS_ITEM_GROUP_MEMBERS = 8;

struct SuspiciousItemGroupEntry {
    float               item_x           = 0.f;
    float               item_z           = 0.f;
    SuspiciousItemType  item_type        = SuspiciousItemType::Unknown;
    SuspiciousItemStage stage            = SuspiciousItemStage::None;
    bool                progress_allowed = false;
    uint8_t             member_count     = 0;
    uint8_t             routing_count    = 0;
    uint8_t             _pad             = 0;
    uint32_t            members[MAX_SUSPICIOUS_ITEM_GROUP_MEMBERS] = {};
};

// ── SuspiciousItemGroupComponent ──────────────────────────────────────────────
// Attach to an NPC entity when it joins a SuspiciousItem group.
// group_id == INVALID_GROUP means unassigned.
struct SuspiciousItemGroupComponent {
    static constexpr uint8_t INVALID_GROUP = 0xFF;
    uint8_t  group_id   = INVALID_GROUP;
    uint8_t  is_routing = 0;  // 1 once this member has started routing
    uint8_t  _pad[6]    = {};
};
static_assert(sizeof(SuspiciousItemGroupComponent) == 8, "SuspiciousItemGroupComponent must be 8 bytes");

// ── SuspiciousItemGroupRegistry ───────────────────────────────────────────────
// Singleton — one per process; safe to call ClearAll() between tests.
class SuspiciousItemGroupRegistry {
public:
    static constexpr uint8_t MAX_GROUPS    = 4;
    static constexpr uint8_t INVALID_GROUP = 0xFF;

    static SuspiciousItemGroupRegistry& Get() noexcept {
        static SuspiciousItemGroupRegistry inst;
        return inst;
    }

    // Find existing group for this item type near (x, z), or allocate a new slot.
    // Returns INVALID_GROUP if no slot available.
    uint8_t FindOrCreate(float x, float z, SuspiciousItemType type,
                         float merge_radius_m = 3.f) noexcept {
        float r2 = merge_radius_m * merge_radius_m;
        for (uint8_t i = 0; i < MAX_GROUPS; ++i) {
            if (!active_[i]) continue;
            if (groups_[i].item_type != type) continue;
            float dx = groups_[i].item_x - x;
            float dz = groups_[i].item_z - z;
            if (dx * dx + dz * dz <= r2) return i;
        }
        for (uint8_t i = 0; i < MAX_GROUPS; ++i) {
            if (!active_[i]) {
                groups_[i]           = {};
                groups_[i].item_x    = x;
                groups_[i].item_z    = z;
                groups_[i].item_type = type;
                active_[i]           = true;
                return i;
            }
        }
        return INVALID_GROUP;
    }

    // Add entity to group. Returns false if group full or id invalid.
    bool Join(uint8_t group_id, uint32_t entity_raw) noexcept {
        if (group_id >= MAX_GROUPS || !active_[group_id]) return false;
        SuspiciousItemGroupEntry& g = groups_[group_id];
        if (g.member_count >= MAX_SUSPICIOUS_ITEM_GROUP_MEMBERS) return false;
        g.members[g.member_count++] = entity_raw;
        return true;
    }

    bool IsFirstMember(uint8_t group_id, uint32_t entity_raw) const noexcept {
        if (group_id >= MAX_GROUPS || !active_[group_id]) return false;
        const SuspiciousItemGroupEntry& g = groups_[group_id];
        return g.member_count > 0 && g.members[0] == entity_raw;
    }

    bool IsAllowedToProgress(uint8_t group_id) const noexcept {
        if (group_id >= MAX_GROUPS || !active_[group_id]) return false;
        return groups_[group_id].progress_allowed;
    }

    bool AllRoutingTo(uint8_t group_id) const noexcept {
        if (group_id >= MAX_GROUPS || !active_[group_id]) return false;
        const SuspiciousItemGroupEntry& g = groups_[group_id];
        return g.member_count > 0 && g.routing_count == g.member_count;
    }

    bool WaitingForRouting(uint8_t group_id) const noexcept {
        if (group_id >= MAX_GROUPS || !active_[group_id]) return false;
        const SuspiciousItemGroupEntry& g = groups_[group_id];
        return g.member_count > 0 && g.routing_count < g.member_count;
    }

    void SetAllowedToProgress(uint8_t group_id, bool v) noexcept {
        if (group_id < MAX_GROUPS && active_[group_id])
            groups_[group_id].progress_allowed = v;
    }

    void MarkRouting(uint8_t group_id, uint32_t entity_raw) noexcept {
        if (group_id >= MAX_GROUPS || !active_[group_id]) return;
        SuspiciousItemGroupEntry& g = groups_[group_id];
        for (uint8_t i = 0; i < g.member_count; ++i) {
            if (g.members[i] == entity_raw) {
                if (g.routing_count < g.member_count) ++g.routing_count;
                return;
            }
        }
    }

    const SuspiciousItemGroupEntry* GetEntry(uint8_t group_id) const noexcept {
        if (group_id >= MAX_GROUPS || !active_[group_id]) return nullptr;
        return &groups_[group_id];
    }

    void ClearAll() noexcept {
        for (uint8_t i = 0; i < MAX_GROUPS; ++i) { groups_[i] = {}; active_[i] = false; }
    }

private:
    SuspiciousItemGroupEntry groups_[MAX_GROUPS] = {};
    bool                     active_[MAX_GROUPS] = {};
};
