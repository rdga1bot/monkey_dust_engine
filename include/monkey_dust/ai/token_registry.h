#pragma once
#include <monkey_dust/ecs/md_entity.h>
// MdTokenRegistry — limits simultaneous NPC roles by token type.
// Mirrors Alien: Isolation ConditionHasToken: at most N entities can hold a
// given token concurrently.  Token IDs are FNV-1a hashes of name strings.
//
// Usage (BT side — prefer builder nodes):
//   bool ok = MdTokenRegistry::Get().AcquireToken(token_id, e);  // idempotent
//   MdTokenRegistry::Get().ReleaseToken(token_id, e);
//   MdTokenRegistry::Get().SetLimit(token_id, 2);   // max 2 concurrent holders
//   MdTokenRegistry::Get().Reset();                 // on level/scene reset

#include <entt/entt.hpp>
#include <cstdint>
#include <cstring>

class MdTokenRegistry {
public:
    static MdTokenRegistry& Get() { static MdTokenRegistry s; return s; }

    static constexpr int MAX_TOKEN_TYPES   = 16;
    static constexpr int MAX_HOLDERS       = 8;
    static constexpr uint8_t DEFAULT_LIMIT = 2;

    // Attempt to acquire token for entity e.
    // Returns true if e already holds it, or count < limit.
    // Returns false if at capacity.
    bool AcquireToken(uint32_t token_id, MdEntity e) {
        int slot = FindOrCreate(token_id);
        if (slot < 0) return false;
        TokenSlot& ts = slots_[slot];
        for (int i = 0; i < MAX_HOLDERS; ++i)
            if (ts.holders[i] == e) return true;  // already held
        if (ts.count >= ts.limit) return false;
        for (int i = 0; i < MAX_HOLDERS; ++i) {
            if (ts.holders[i] == MdEntity::Null()) {
                ts.holders[i] = e;
                ++ts.count;
                return true;
            }
        }
        return false;
    }

    // Release token held by entity e (no-op if e doesn't hold it).
    void ReleaseToken(uint32_t token_id, MdEntity e) {
        int slot = Find(token_id);
        if (slot < 0) return;
        TokenSlot& ts = slots_[slot];
        for (int i = 0; i < MAX_HOLDERS; ++i) {
            if (ts.holders[i] == e) {
                ts.holders[i] = MdEntity::Null();
                if (ts.count > 0) --ts.count;
                return;
            }
        }
    }

    // Set maximum concurrent holders for token_id (default: DEFAULT_LIMIT).
    void SetLimit(uint32_t token_id, uint8_t limit) {
        int slot = FindOrCreate(token_id);
        if (slot >= 0) slots_[slot].limit = limit;
    }

    // Clear all token counts and holders (call on scene reload).
    void Reset() {
        for (int i = 0; i < MAX_TOKEN_TYPES; ++i) {
            slots_[i].id    = 0;
            slots_[i].count = 0;
            slots_[i].limit = DEFAULT_LIMIT;
            for (int j = 0; j < MAX_HOLDERS; ++j)
                slots_[i].holders[j] = MdEntity::Null();
        }
    }

private:
    struct TokenSlot {
        uint32_t   id                  = 0;
        uint8_t    count               = 0;
        uint8_t    limit               = DEFAULT_LIMIT;
        MdEntity holders[MAX_HOLDERS];
    };

    MdTokenRegistry() { Reset(); }

    int Find(uint32_t id) const {
        for (int i = 0; i < MAX_TOKEN_TYPES; ++i)
            if (slots_[i].id == id && id != 0) return i;
        return -1;
    }

    int FindOrCreate(uint32_t id) {
        int f = Find(id);
        if (f >= 0) return f;
        for (int i = 0; i < MAX_TOKEN_TYPES; ++i) {
            if (slots_[i].id == 0) { slots_[i].id = id; return i; }
        }
        return -1;  // table full
    }

    TokenSlot slots_[MAX_TOKEN_TYPES];
};
