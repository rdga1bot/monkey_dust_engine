#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <entt/entt.hpp>
#include <cstdint>

// ── C18: RoleSystem ───────────────────────────────────────────────────────────
// Prevents multiple NPCs competing for the same exclusive behaviour slot
// (e.g. only one NPC in Stalk at a time, only one in SuspectMoveTo).
//
// Usage in BT:
//   RoleCheck(role, check_could_perform=false) → Success if entity owns the slot
//   RoleCheck(role, check_could_perform=true)  → Success if slot is free
//   RoleClaim(role, query_id)                  → atomically claims; Success or Failure
//   RoleRelease(role)                          → always Success; frees the slot

enum class NpcRole : uint8_t {
    None            = 0,
    Stalk           = 1,  // STALK:4 — silent pursuit
    SuspectMoveTo   = 2,  // SUSPECT_RESPONSE_MOVE_TO:8 — investigate stimulus
    Flanker         = 3,  // flanking approach
    SearchLead      = 4,  // leads group search
    AmbushWait      = 5,  // holding ambush position
    Backstage       = 6,  // patrolling backstage
    CorpseTrap      = 7,  // guarding corpse
    COUNT           = 8,
};
static constexpr int MAX_ROLES = static_cast<int>(NpcRole::COUNT);

struct RoleSlot {
    MdEntity owner    = entt::null;
    uint32_t     query_id = 0;
};

class RoleRegistry {
public:
    static RoleRegistry& Get() noexcept {
        static RoleRegistry s_instance;
        return s_instance;
    }

    // Attempt to claim a role for entity e with given query_id.
    // Returns true (Success) only if the slot is currently free.
    // Already owning the same role (same entity) refreshes query_id and returns true.
    bool claim(NpcRole role, uint32_t query_id, MdEntity e) noexcept {
        int idx = static_cast<int>(role);
        if (idx <= 0 || idx >= MAX_ROLES) return false;
        RoleSlot& s = m_slots[idx];
        if (s.owner == entt::null || s.owner == e) {
            s.owner    = e;
            s.query_id = query_id;
            return true;
        }
        return false;
    }

    // Release the slot owned by e. No-op if e doesn't own it.
    void release(NpcRole role, MdEntity e) noexcept {
        int idx = static_cast<int>(role);
        if (idx <= 0 || idx >= MAX_ROLES) return;
        if (m_slots[idx].owner == e)
            m_slots[idx] = RoleSlot{};
    }

    // Returns true if e currently owns the role slot.
    bool is_performing(NpcRole role, MdEntity e) const noexcept {
        int idx = static_cast<int>(role);
        if (idx <= 0 || idx >= MAX_ROLES) return false;
        return m_slots[idx].owner == e;
    }

    // Returns true if the role slot is unclaimed (available to anyone).
    bool could_perform(NpcRole role) const noexcept {
        int idx = static_cast<int>(role);
        if (idx <= 0 || idx >= MAX_ROLES) return false;
        return m_slots[idx].owner == entt::null;
    }

    // Release all slots owned by e (e.g. on NPC death/despawn).
    void release_all(MdEntity e) noexcept {
        for (int i = 1; i < MAX_ROLES; ++i)
            if (m_slots[i].owner == e)
                m_slots[i] = RoleSlot{};
    }

    void reset() noexcept {
        for (int i = 0; i < MAX_ROLES; ++i) m_slots[i] = RoleSlot{};
    }

private:
    RoleRegistry() noexcept { reset(); }
    RoleSlot m_slots[MAX_ROLES];
};
