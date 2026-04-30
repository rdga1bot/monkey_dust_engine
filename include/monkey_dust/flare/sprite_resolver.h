#pragma once
#include <monkey_dust/flare/sprite_animation.h>

namespace md::flare {

constexpr int MAX_SPRITE_CATEGORIES = 64;

struct SpriteCategoryEntry {
    char          category[32];
    SpriteAnimSet anim_set;
    char          atlas_full_path[256];
    bool          valid;
};

class SpriteResolver {
public:
    static SpriteResolver& Get();

    // Resolve category → atlas path + anim set (cached).
    const SpriteCategoryEntry* Resolve(const char* category);

    void Clear();

    // Get first [stance] frame, dir=0.  Returns false if unavailable.
    bool GetStanceFrame0(const char* category, SpriteFrame& out_frame) const;

private:
    SpriteCategoryEntry entries_[MAX_SPRITE_CATEGORIES];
    int                 entry_count_ = 0;
};

} // namespace md::flare
