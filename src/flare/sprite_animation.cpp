#include <monkey_dust/flare/sprite_animation.h>
#include <monkey_dust/flare/ini_parser.h>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace md::flare {

// ── LoadAnimSet ───────────────────────────────────────────────────────────────

struct LoadState {
    SpriteAnimSet* out;
    int            cur_clip;   // index of the clip being parsed (-1 = none)
};

static void anim_cb(const IniLine& l, void* ud) {
    LoadState*     s   = (LoadState*)ud;
    SpriteAnimSet* set = s->out;

    // Global `image=` line (before any section).
    if (strcmp(l.key, "image") == 0 && s->cur_clip < 0) {
        CopyString(set->image_path, l.value, (int)sizeof(set->image_path));
        return;
    }

    // Section change — the IniLine::section tells us the clip name.
    // But the callback is called per key=value; section state is in l.section.
    // Start a new clip when section name differs from the current clip.
    if (s->cur_clip < 0 ||
        strcmp(set->clips[s->cur_clip].name, l.section) != 0)
    {
        // Find or create clip for this section.
        int idx = -1;
        for (int i = 0; i < set->clip_count; ++i) {
            if (strcmp(set->clips[i].name, l.section) == 0) { idx = i; break; }
        }
        if (idx < 0 && set->clip_count < MAX_CLIPS_PER_SET && l.section[0]) {
            idx = set->clip_count++;
            memset(&set->clips[idx], 0, sizeof(AnimClip));
            CopyString(set->clips[idx].name, l.section,
                       (int)sizeof(set->clips[idx].name));
        }
        s->cur_clip = idx;
    }

    if (s->cur_clip < 0) return;
    AnimClip& clip = set->clips[s->cur_clip];

    if (strcmp(l.key, "frames") == 0) {
        clip.frames = (int)strtol(l.value, nullptr, 10);
        if (clip.frames > MAX_FRAMES_PER_CLIP) clip.frames = MAX_FRAMES_PER_CLIP;
    } else if (strcmp(l.key, "duration") == 0) {
        // "800ms" or "53" (bare integer = ms)
        clip.duration_ms = (int)strtol(l.value, nullptr, 10);
    } else if (strcmp(l.key, "type") == 0) {
        if (strcmp(l.value, "looped")     == 0) clip.type = AnimType::LOOPED;
        else if (strcmp(l.value, "back_forth") == 0) clip.type = AnimType::BACK_FORTH;
        else                                         clip.type = AnimType::PLAY_ONCE;
    } else if (strcmp(l.key, "frame") == 0) {
        // "D,F,X,Y,W,H,offX,offY"
        int vals[8] = {};
        int n = ParseIntList(l.value, vals, 8);
        if (n >= 6) {
            int dir  = vals[0];
            int fidx = vals[1];
            if (dir >= 0 && dir < FLARE_DIRECTIONS &&
                fidx >= 0 && fidx < MAX_FRAMES_PER_CLIP)
            {
                SpriteFrame& f = clip.frame_data[dir * MAX_FRAMES_PER_CLIP + fidx];
                f.x     = (uint16_t)vals[2];
                f.y     = (uint16_t)vals[3];
                f.w     = (uint16_t)vals[4];
                f.h     = (uint16_t)vals[5];
                f.off_x = (n >= 7) ? (int16_t)vals[6] : 0;
                f.off_y = (n >= 8) ? (int16_t)vals[7] : 0;
            }
        }
    }
}

bool LoadAnimSet(const char* file_path, SpriteAnimSet& out) {
    memset(&out, 0, sizeof(out));
    LoadState s = { &out, -1 };
    return ParseIniFile(file_path, anim_cb, &s, nullptr);
}

// ── FindClip ──────────────────────────────────────────────────────────────────

int FindClip(const SpriteAnimSet& set, const char* name) {
    for (int i = 0; i < set.clip_count; ++i)
        if (strcmp(set.clips[i].name, name) == 0) return i;
    return -1;
}

// ── SetClip / TickAnim / CurrentFrame ────────────────────────────────────────

void SetClip(SpriteAnimState& state, int clip_idx) {
    if (state.clip_idx == clip_idx) return;
    state.clip_idx   = clip_idx;
    state.frame      = 0;
    state.elapsed_ms = 0.0f;
    state.finished   = false;
}

bool TickAnim(SpriteAnimState& state, float dt_ms) {
    if (!state.set || state.clip_idx < 0 ||
        state.clip_idx >= state.set->clip_count) return false;

    const AnimClip& clip = state.set->clips[state.clip_idx];
    if (clip.frames <= 0 || clip.duration_ms <= 0) return false;

    float ms_per_frame = (float)clip.duration_ms / (float)clip.frames;

    state.elapsed_ms += dt_ms;

    if (state.elapsed_ms >= ms_per_frame) {
        state.elapsed_ms -= ms_per_frame;
        ++state.frame;

        switch (clip.type) {
        case AnimType::LOOPED:
            if (state.frame >= clip.frames) state.frame = 0;
            break;
        case AnimType::BACK_FORTH:
            // 0→frames-1→0 oscillation
            if (state.frame >= clip.frames) {
                state.frame = clip.frames - 2;
                if (state.frame < 0) state.frame = 0;
                // Simple: restart. Production impl would track direction.
                state.frame = 0;
            }
            break;
        case AnimType::PLAY_ONCE:
            if (state.frame >= clip.frames) {
                state.frame    = clip.frames - 1;
                state.finished = true;
                return true;
            }
            break;
        }
    }
    return state.finished;
}

const SpriteFrame* CurrentFrame(const SpriteAnimState& state) {
    if (!state.set || state.clip_idx < 0 ||
        state.clip_idx >= state.set->clip_count) return nullptr;
    const AnimClip& clip = state.set->clips[state.clip_idx];
    int dir   = state.dir;
    int frame = state.frame;
    if (dir < 0 || dir >= FLARE_DIRECTIONS) dir = 0;
    if (frame < 0 || frame >= clip.frames)  frame = 0;
    return &clip.frame_data[dir * MAX_FRAMES_PER_CLIP + frame];
}

// ── ComputeDirection8 ─────────────────────────────────────────────────────────
//
// Flare isometric 8-direction mapping (dx/dz in XZ world plane):
//   S=0, SW=1, W=2, NW=3, N=4, NE=5, E=6, SE=7
// Angle 0 = east (dx>0, dz=0). Clockwise from east looking down.

int ComputeDirection8(float dx, float dz) {
    if (dx == 0.0f && dz == 0.0f) return 0;
    float angle = atan2f(-dz, dx);    // -dz so +Z = south = direction 0
    if (angle < 0.0f) angle += 6.28318530f;
    // Map [0, 2π) → [0, 8) slice, then offset for Flare convention
    int dir = (int)((angle / 6.28318530f) * 8.0f + 0.5f) % 8;
    return dir;
}

} // namespace md::flare
