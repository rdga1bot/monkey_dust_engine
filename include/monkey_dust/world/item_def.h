#pragma once
// item_def.h — Static item database (Phase 4, Task 4.3)
// Populated by tools/flare_convert/kenshi_data_parser from gamedata.base
// Runtime: ItemDatabase::LoadFromJson() on startup.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static constexpr int MAX_ITEM_DEFS = 2048;

enum class ItemType : uint8_t {
    None      = 0,
    Weapon    = 1,
    Armour    = 2,
    Food      = 3,
    Material  = 4,
    Tool      = 5,
    Prosthetic= 6,
    Blueprint = 7,
};

enum class ItemLimbSlot : uint8_t {
    None  = 0,
    Head  = 1,
    Torso = 2,
    LArm  = 3,
    RArm  = 4,
    LLeg  = 5,
    RLeg  = 6,
};

struct ItemDef {
    uint32_t     id;
    char         name[32];
    float        weight;
    float        value;          // trade cats
    ItemType     item_type;
    ItemLimbSlot limb_slot;
    uint8_t      _pad[2];
    // Weapon
    float cut_damage;
    float blunt_damage;
    float attack_speed;
    float reach;
    // Armour
    float cut_resist;
    float blunt_resist;
    float armour_grade;
    // Food
    float nutrition;
    // Prosthetic
    float prosthetic_hp;
    float prosthetic_grade;
    bool  loaded;
    uint8_t _pad2[3];
};
static_assert(sizeof(ItemDef) == 92, "ItemDef layout changed");

// ── ItemDatabase singleton ────────────────────────────────────────────────────
class ItemDatabase {
public:
    static ItemDatabase& Get() { static ItemDatabase s; return s; }

    int LoadFromJson(const char* path) {
        FILE* f = ::fopen(path, "rb");
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        long fsz = ftell(f);
        rewind(f);
        if (fsz <= 0 || fsz > 4*1024*1024) { fclose(f); return 0; }
        char* buf = new char[(size_t)fsz + 1];
        size_t rd = fread(buf, 1, (size_t)fsz, f);
        fclose(f);
        buf[rd] = 0;

        count_ = 0;
        const char* p = buf;
        while (count_ < MAX_ITEM_DEFS) {
            const char* nk = strstr(p, "\"name\":");
            if (!nk) break;
            ItemDef& it = defs_[count_];
            memset(&it, 0, sizeof(it));
            it.id = (uint32_t)count_;
            const char* vs = strchr(nk + 7, '"');
            if (!vs) { p = nk + 7; continue; }
            const char* ve = strchr(vs + 1, '"');
            if (!ve) { p = vs + 1; continue; }
            int nl = (int)(ve - vs - 1); if (nl > 31) nl = 31;
            memcpy(it.name, vs + 1, nl);
            auto ri = [&](const char* key, int def = 0) -> int {
                const char* kp = strstr(p, key);
                return (kp && kp < nk + 512) ? (int)strtol(kp + strlen(key), nullptr, 10) : def;
            };
            auto rf = [&](const char* key) -> float {
                const char* kp = strstr(p, key);
                return (kp && kp < nk + 512) ? (float)strtod(kp + strlen(key), nullptr) : 0.f;
            };
            it.item_type     = (ItemType)(uint8_t)ri("\"item_type\":");
            it.weight        = rf("\"weight\":");
            it.value         = rf("\"value\":");
            it.cut_damage    = rf("\"cut_damage\":");
            it.blunt_damage  = rf("\"blunt_damage\":");
            it.attack_speed  = rf("\"attack_speed\":");
            it.reach         = rf("\"reach\":");
            it.cut_resist    = rf("\"cut_resist\":");
            it.blunt_resist  = rf("\"blunt_resist\":");
            it.armour_grade  = rf("\"armour_grade\":");
            it.nutrition     = rf("\"nutrition\":");
            it.prosthetic_hp = rf("\"prosthetic_hp\":");
            it.loaded = true;
            count_++;
            p = ve + 1;
        }
        delete[] buf;
        return count_;
    }

    const ItemDef* Find(uint32_t id) const {
        if (id < (uint32_t)count_) return &defs_[id];
        return nullptr;
    }
    const ItemDef* FindByName(const char* name) const {
        for (int i = 0; i < count_; ++i)
            if (!strcmp(defs_[i].name, name)) return &defs_[i];
        return nullptr;
    }
    int Count() const { return count_; }

private:
    ItemDatabase() { memset(defs_, 0, sizeof(defs_)); }
    ItemDef defs_[MAX_ITEM_DEFS];
    int     count_ = 0;
};
