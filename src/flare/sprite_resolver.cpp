#include <monkey_dust/flare/sprite_resolver.h>
#include <monkey_dust/flare/flare_runtime.h>
#include <cstdio>
#include <cstring>
#include "raylib.h"   // TraceLog

namespace md::flare {

SpriteResolver& SpriteResolver::Get() {
    static SpriteResolver inst;
    return inst;
}

void SpriteResolver::Clear() {
    entry_count_ = 0;
    memset(entries_, 0, sizeof(entries_));
}

const SpriteCategoryEntry* SpriteResolver::Resolve(const char* category) {
    if (!category || category[0] == '\0') return nullptr;

    for (int i = 0; i < entry_count_; ++i) {
        if (strcmp(entries_[i].category, category) == 0)
            return entries_[i].valid ? &entries_[i] : nullptr;
    }

    if (entry_count_ >= MAX_SPRITE_CATEGORIES) {
        TraceLog(LOG_WARNING, "[SpriteResolver] cache full, %s skipped", category);
        return nullptr;
    }

    SpriteCategoryEntry& e = entries_[entry_count_++];
    memset(&e, 0, sizeof(e));
    strncpy(e.category, category, sizeof(e.category) - 1);

    // Resolve animations/enemies/<category>.txt through mod chain.
    char rel[256];
    snprintf(rel, sizeof(rel), "animations/enemies/%s.txt", category);
    char full_anim[256];
    if (!FlareRuntime::Get().ResolveAsset(rel, full_anim, sizeof(full_anim))) {
        TraceLog(LOG_WARNING, "[SpriteResolver] %s: anim file not found", category);
        e.valid = false;
        return nullptr;
    }

    if (!LoadAnimSet(full_anim, e.anim_set)) {
        TraceLog(LOG_WARNING, "[SpriteResolver] %s: LoadAnimSet failed", category);
        e.valid = false;
        return nullptr;
    }

    // Resolve the atlas image through mod chain.
    if (!FlareRuntime::Get().ResolveAsset(e.anim_set.image_path,
                                          e.atlas_full_path,
                                          sizeof(e.atlas_full_path))) {
        TraceLog(LOG_WARNING, "[SpriteResolver] %s: atlas %s not found",
                 category, e.anim_set.image_path);
        e.valid = false;
        return nullptr;
    }

    e.valid = true;
    TraceLog(LOG_INFO, "[SpriteResolver] %s OK: %d clips, atlas=%s",
             category, e.anim_set.clip_count, e.atlas_full_path);
    return &e;
}

bool SpriteResolver::GetStanceFrame0(const char* category, SpriteFrame& out_frame) const {
    const SpriteCategoryEntry* e = nullptr;
    for (int i = 0; i < entry_count_; ++i) {
        if (strcmp(entries_[i].category, category) == 0 && entries_[i].valid) {
            e = &entries_[i];
            break;
        }
    }
    if (!e) return false;

    int stance_idx = FindClip(e->anim_set, "stance");
    if (stance_idx < 0 && e->anim_set.clip_count > 0) stance_idx = 0;
    if (stance_idx < 0) return false;

    const AnimClip& clip = e->anim_set.clips[stance_idx];
    if (clip.frames <= 0) return false;

    out_frame = clip.frame_data[0];   // dir=0, frame=0
    return true;
}

} // namespace md::flare
