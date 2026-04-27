#pragma once
#include <cstdint>

namespace md::flare {

constexpr int MAX_POWERS = 512;

struct PowerDef {
    int   id;
    char  name[64];
    char  type[24];      // "fixed", "block", "transform", …
    int   icon;
    int   requires_mp;
    float cooldown_s;
    float radius;
    float lifespan_s;
    bool  buff;
    bool  use_hazard;
    bool  passive;
    bool  valid;
};

struct PowerRegistry {
    PowerDef defs[MAX_POWERS];
    int      count;
};

// Parse <mod_chain[0]>/powers/powers.txt following INCLUDE directives (≤ 3 deep).
// INCLUDE paths are searched across the full mod chain (child → base).
// Returns false when powers.txt cannot be opened in chain[0].
bool LoadPowers(const char* const* mod_chain, int n, PowerRegistry& out);

inline bool LoadPowers(const char* mod_path, PowerRegistry& out) {
    return LoadPowers(&mod_path, 1, out);
}

// Linear search by id.
const PowerDef* FindPower(const PowerRegistry& reg, int id);

} // namespace md::flare
