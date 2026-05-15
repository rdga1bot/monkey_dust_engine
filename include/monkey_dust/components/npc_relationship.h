#pragma once
#include <cstdint>
#include <entt/entt.hpp>
#include <monkey_dust/platform/md_log.h>

// ── NpcRelationshipComponent ──────────────────────────────────────────────────
// Echo-engine inspired inter-NPC relationship system.
// Tracks bilateral trust/fear values between specific NPC pairs.
// Orthogonal to AllianceMatrix (faction-level) — this is per-entity.
//
// trust: 0=hostile, 128=neutral, 255=fully trusted
// fear:  0=no fear, 255=maximum fear (triggers Flee motivation)
//
// MAX_RELATIONS per entity: 8 pairs; silent LRU eviction when full.
// entt::null in entity field = empty slot.

struct NpcRelation {
    entt::entity entity  = entt::null;
    uint8_t      trust   = 128;   // 128=neutral
    uint8_t      fear    = 0;
    uint8_t      _pad[2] = {};
};
static_assert(sizeof(NpcRelation) == 8, "NpcRelation must be 8 bytes");

struct NpcRelationshipComponent {
    static constexpr uint8_t MAX_RELATIONS = 8;

    NpcRelation slots[MAX_RELATIONS] = {};
    uint8_t     count                = 0;
    uint8_t     _pad[3]              = {};

    // Find or create a slot for `other`. Returns nullptr only if all slots are
    // occupied by different entities (caller can evict manually if needed).
    NpcRelation* FindOrCreate(entt::entity other) noexcept {
        for (uint8_t i = 0; i < count; ++i)
            if (slots[i].entity == other) return &slots[i];
        if (count < MAX_RELATIONS) {
            slots[count] = NpcRelation{other, 128, 0, {}};
            return &slots[count++];
        }
        MD_LOG(MD_LOG_WARNING, "NpcRelationshipComponent: full, evicting slot 0");
        for (uint8_t i = 0; i < MAX_RELATIONS - 1; ++i) slots[i] = slots[i + 1];
        slots[MAX_RELATIONS - 1] = NpcRelation{other, 128, 0, {}};
        return &slots[MAX_RELATIONS - 1];
    }

    const NpcRelation* Find(entt::entity other) const noexcept {
        for (uint8_t i = 0; i < count; ++i)
            if (slots[i].entity == other) return &slots[i];
        return nullptr;
    }

    void SetTrust(entt::entity other, uint8_t val) noexcept {
        if (NpcRelation* r = FindOrCreate(other)) r->trust = val;
    }
    void SetFear(entt::entity other, uint8_t val) noexcept {
        if (NpcRelation* r = FindOrCreate(other)) r->fear = val;
    }

    // Returns 128 (neutral) when no relation exists.
    uint8_t GetTrust(entt::entity other) const noexcept {
        const NpcRelation* r = Find(other);
        return r ? r->trust : 128u;
    }
    // Returns 0 (no fear) when no relation exists.
    uint8_t GetFear(entt::entity other) const noexcept {
        const NpcRelation* r = Find(other);
        return r ? r->fear : 0u;
    }

    void ClearAll() noexcept { count = 0; }
};
static_assert(sizeof(NpcRelationshipComponent) == 68, "NpcRelationshipComponent must be 68 bytes");
