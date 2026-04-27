#pragma once
#include <cstdint>

namespace md::flare {

// ── Sprite sheet frame region ─────────────────────────────────────────────────
// Matches one `frame=D,F,X,Y,W,H,offX,offY` line from animations/<name>.txt.
struct SpriteFrame {
    uint16_t x, y, w, h;       // pixel rect in sprite sheet
    int16_t  off_x, off_y;     // render offset (center adjustment)
};

// ── Animation clip ────────────────────────────────────────────────────────────
// One [section] in a Flare animation file: stance, run, swing, die, …
// Frame data for all 8 directions is stored flat:
//   frame_data[direction * frames + frame_idx]

constexpr int FLARE_DIRECTIONS = 8;
constexpr int MAX_FRAMES_PER_CLIP = 24;   // max frames per direction

enum class AnimType : uint8_t {
    LOOPED,
    BACK_FORTH,
    PLAY_ONCE,
};

struct AnimClip {
    char     name[32];
    int      frames;               // frames per direction
    int      duration_ms;          // total duration of full cycle
    AnimType type;
    // frame_data[dir * frames + frame_idx], valid for dir in [0, FLARE_DIRECTIONS)
    // and frame_idx in [0, frames)
    SpriteFrame frame_data[FLARE_DIRECTIONS * MAX_FRAMES_PER_CLIP];
};

// ── Animation set ─────────────────────────────────────────────────────────────
// Loaded from one animations/<name>.txt file; covers one entity type.

constexpr int MAX_CLIPS_PER_SET = 16;

struct SpriteAnimSet {
    char     image_path[128];          // relative to mod root: "images/enemies/goblin.png"
    AnimClip clips[MAX_CLIPS_PER_SET];
    int      clip_count;
};

// ── Per-entity animation state (ECS component) ────────────────────────────────
// Lives in game-side components/flare_sprite_anim.h, but state machine is here.

struct SpriteAnimState {
    const SpriteAnimSet* set;        // pointer into global anim registry (not owned)
    int                  clip_idx;
    int                  frame;
    int                  dir;        // 0..7
    float                elapsed_ms;
    bool                 finished;   // true when PLAY_ONCE animation completes
};

// ── API ───────────────────────────────────────────────────────────────────────

// Load all clips from one Flare animation .txt file.
bool LoadAnimSet(const char* file_path, SpriteAnimSet& out);

// Find clip index by name. Returns -1 if not found.
int  FindClip(const SpriteAnimSet& set, const char* name);

// Advance animation state by dt_ms milliseconds. Returns true when PLAY_ONCE ends.
bool TickAnim(SpriteAnimState& state, float dt_ms);

// Change clip (resets frame/elapsed). No-op if already on same clip.
void SetClip(SpriteAnimState& state, int clip_idx);

// Get current frame region for rendering.
// Returns nullptr if state is invalid.
const SpriteFrame* CurrentFrame(const SpriteAnimState& state);

// Compute direction 0..7 from a velocity vector (dx, dz in world space).
// Convention: 0 = south, 1 = SW, 2 = W, … 7 = SE (Flare isometric order).
int ComputeDirection8(float dx, float dz);

} // namespace md::flare
