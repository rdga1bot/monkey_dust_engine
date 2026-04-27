#pragma once
#include <monkey_dust/render/ssbo.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static constexpr int MAX_BONES        = 128;
static constexpr int MAX_ANIMATED_NPC = 500;
static constexpr int MAX_ANIM_CLIPS   = 8;

// Per-NPC animation state — 16 bytes, matches std430 AnimState in skinning.comp
struct AnimNpcState {
    uint32_t slot;
    uint32_t clip_id;  // 0=IDLE, 1=WALK, 2=ATTACK
    float    time_s;
    float    _pad;
};
static_assert(sizeof(AnimNpcState) == 16, "AnimNpcState size mismatch");

struct AnimationClip {
    char    name[32];
    uint8_t id;
    float   duration_s;
    int     frame_count;
};

// ─────────────────────────────────────────────────────────
// AnimationSoA — singleton: per-NPC anim state + GPU SSBOs.
//
// SSBO layout:
//   binding 4: mat4[MAX_ANIMATED_NPC × MAX_BONES]  ← written by skinning.comp
//   binding 5: AnimNpcState[MAX_ANIMATED_NPC]       ← read by skinning.comp
//
// Usage:
//   Init()               — once after window open
//   LoadClipsFromFile()  — loads animation clips JSON
//   SetClip(slot, id)    — per logic tick, per NPC
//   Advance(dt)          — per logic tick
//   Upload()             — per render frame, binds SSBOs
//   BindSSBOs()          — re-bind after other draws may rebind
//   Shutdown()           — on exit
// ─────────────────────────────────────────────────────────

class AnimationSoA {
public:
    static AnimationSoA& Get() {
        static AnimationSoA inst;
        return inst;
    }

    void Init() {
        memset(states_, 0, sizeof(states_));
        for (int i = 0; i < MAX_ANIMATED_NPC; ++i)
            states_[i].slot = (uint32_t)i;
        // mat4 = 64 bytes; finalBoneMatrices: MAX_ANIMATED_NPC * MAX_BONES * 64
        bones_ssbo_.Init(MAX_ANIMATED_NPC * MAX_BONES * 64);
        anim_state_ssbo_.Init(MAX_ANIMATED_NPC * (int)sizeof(AnimNpcState));
        LoadDefaults();
    }

    int LoadClipsFromFile(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return clips_count_;
        static char buf[2048];
        int n = (int)fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        clips_count_ = 0;
        const char* cur = buf;
        while (clips_count_ < MAX_ANIM_CLIPS) {
            const char* p = strstr(cur, "\"id\"");
            if (!p) break;
            AnimationClip& c = clips_[clips_count_];
            memset(&c, 0, sizeof(c));
            int id_v = 0, fc = 24;
            float dur = 1.0f;
            ParseInt(p, "\"id\"",       id_v);
            ParseFloat(p, "\"duration\"", dur);
            ParseInt(p, "\"frames\"",   fc);
            ParseStr(p, "\"name\"",     c.name, 32);
            c.id = (uint8_t)id_v;
            c.duration_s  = dur;
            c.frame_count = fc;
            clips_count_++;
            cur = p + 4;
        }
        if (clips_count_ == 0) LoadDefaults();
        return clips_count_;
    }

    void SetClip(int slot, uint8_t clip_id) {
        if (slot < 0 || slot >= MAX_ANIMATED_NPC) return;
        if (states_[slot].clip_id == clip_id) return;
        states_[slot].clip_id = clip_id;
        states_[slot].time_s  = 0.0f;
    }

    void Advance(float dt) {
        for (int i = 0; i < MAX_ANIMATED_NPC; ++i) {
            uint32_t cid = states_[i].clip_id;
            float dur = (cid < (uint32_t)clips_count_) ? clips_[cid].duration_s : 1.0f;
            states_[i].time_s += dt;
            if (states_[i].time_s >= dur) states_[i].time_s -= dur;
        }
    }

    void Upload() {
        anim_state_ssbo_.Upload(states_, MAX_ANIMATED_NPC * (int)sizeof(AnimNpcState));
        anim_state_ssbo_.Bind(5);
        bones_ssbo_.Bind(4);
    }

    void BindSSBOs() {
        anim_state_ssbo_.Bind(5);
        bones_ssbo_.Bind(4);
    }

    void Shutdown() {
        bones_ssbo_.Shutdown();
        anim_state_ssbo_.Shutdown();
    }

    // Accessors for editor panels (replaces direct field access — БОРГ-7/8).
    AnimNpcState&       GetState(int slot)       { return states_[slot]; }
    const AnimNpcState& GetState(int slot) const { return states_[slot]; }
    int                 ClipCount()        const { return clips_count_; }
    const AnimationClip& GetClip(int i)   const { return clips_[i]; }

private:
    AnimationSoA() = default;

    AnimNpcState  states_[MAX_ANIMATED_NPC];
    AnimationClip clips_[MAX_ANIM_CLIPS];
    int           clips_count_ = 0;

    SSBO bones_ssbo_;
    SSBO anim_state_ssbo_;

    void LoadDefaults() {
        clips_count_ = 3;
        auto set = [&](int i, const char* n, uint8_t id, float dur, int frames) {
            strncpy(clips_[i].name, n, 31);
            clips_[i].id = id; clips_[i].duration_s = dur; clips_[i].frame_count = frames;
        };
        set(0, "idle",   0, 2.0f, 60);
        set(1, "walk",   1, 0.8f, 24);
        set(2, "attack", 2, 0.5f, 15);
    }

    static bool ParseInt(const char* p, const char* key, int& out) {
        const char* f = strstr(p, key);
        if (!f) return false;
        f += strlen(key);
        while (*f && (*f == '"' || *f == ':' || *f == ' ')) f++;
        if (!*f) return false;
        out = (int)strtol(f, nullptr, 10);
        return true;
    }
    static bool ParseFloat(const char* p, const char* key, float& out) {
        const char* f = strstr(p, key);
        if (!f) return false;
        f += strlen(key);
        while (*f && (*f == '"' || *f == ':' || *f == ' ')) f++;
        if (!*f) return false;
        out = (float)strtod(f, nullptr);
        return true;
    }
    static bool ParseStr(const char* p, const char* key, char* out, int max) {
        const char* f = strstr(p, key);
        if (!f) return false;
        f += strlen(key);
        const char* q = strchr(f, '"');
        if (!q) return false;
        q++;
        int i = 0;
        while (*q && *q != '"' && i < max - 1) out[i++] = *q++;
        out[i] = '\0';
        return true;
    }
};
