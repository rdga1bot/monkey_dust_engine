#pragma once
#include <monkey_dust/platform/md_hints.h>
#include <monkey_dust/render/ssbo.h>
#include <monkey_dust/render/gpu_ring_buffer.h>
#include <monkey_dust/render/gpu_hal.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── GPU skinning data structs (Step 3) ──────────────────────────────────────
// Max keyframes packed into GpuClipBuf (compact layout, not MAX_SKIN_KF flat).
static constexpr int MAX_GPU_KF = 16384;  // 16384 × 32 bytes = 512 KB

// Per-keyframe data for skinning.comp (8 floats, 32 bytes).
// alignas(16): VBfA pattern — animation float arrays 16-byte aligned for SIMD batch ops.
struct alignas(16) GpuKeyframe { float t, tx, ty, tz, qx, qy, qz, qw; };

// Per-track header: offset + count in flat GpuKeyframe array.
struct GpuTrackHeader { int kf_start, kf_count; };

// Per-clip header.
struct GpuClipHeader { float duration; int bone_count, track_start, _pad; };

// Per-NPC animation state uploaded each frame from AnimatorComponent.
// Sized to 32 bytes (std430 aligned). alignas(16) ensures SIMD-safe batch memcpy.
struct alignas(16) NpcGpuAnim {
    int   clip_hi;      // upper/full walk clip (-1 = use idle)
    int   clip_lo;      // lower body override clip (-1 = no override)
    int   clip_idle;    // idle clip (0 = fallback)
    int   lod_tier;     // 0-4
    float blend_t;      // 0.0=idle … 1.0=walk
    float phase;        // walk/jog phase (accumulated while moving)
    float phase_idle;   // idle phase (absolute time-based)
    float _pad;
};
static_assert(sizeof(NpcGpuAnim) == 32, "NpcGpuAnim must be 32 bytes");

static constexpr int MAX_BONES        = 64;  // SSBO stride fixed at 64 for SDL_GPU transfer alignment; Kenshi uses 30 of 64
static constexpr int MAX_ANIMATED_NPC = 500;
static constexpr int MAX_ANIM_CLIPS   = 8;

// VBfA lines 9042-9043: hard cap on draw calls per renderer stream.
// 2 streams × 200 objects = 400 max draw calls/frame.
static constexpr int MAX_DRAW_CALLS_PER_STREAM = 200;
static constexpr int MAX_DRAW_STREAMS          = 2;
static constexpr int MAX_DRAW_CALLS_TOTAL      = MAX_DRAW_CALLS_PER_STREAM * MAX_DRAW_STREAMS;

// Per-NPC animation state — 16 bytes, matches std430 AnimState in skinning.comp
struct AnimNpcState {
    uint32_t slot;
    uint32_t clip_id;   // 0=IDLE, 1=WALK, 2=ATTACK
    float    time_s;
    uint32_t lod_tier;  // 0-2: skinning.comp runs; 3-4: skip (stale pose)
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
        // bones_ssbo_ is written by CPU/compute and read in vertex shaders.
        bones_ssbo_.Init(MAX_ANIMATED_NPC * MAX_BONES * 64,
                         SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
        // anim_state_ring_ is written by CPU each frame — ring-buffered.
        anim_state_ring_.Init((uint32_t)(MAX_ANIMATED_NPC * (int)sizeof(AnimNpcState)), 5);
        // Skinning compute pipeline — loaded once, dispatched each frame.
        // SDL_GPU: rw[0]=FinalBones(write), ro[0]=AnimState(read); no UBO (time in state).
        GpuComputePipeline::Desc skin_desc;
        skin_desc.glsl_path                     = "shaders/skinning.comp";
        skin_desc.num_readwrite_storage_buffers = 1;  // FinalBones
        skin_desc.num_readonly_storage_buffers  = 3;  // NpcGpuAnim + ClipBuf + SkelBuf
        skin_pipeline_.Create(skin_desc);
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
        if (MD_UNLIKELY(slot < 0 || slot >= MAX_ANIMATED_NPC)) return;
        if (states_[slot].clip_id == clip_id) return;
        states_[slot].clip_id = clip_id;
        states_[slot].time_s  = 0.0f;
    }

    void Advance(float dt) {
        // PERF-16: stagger animation time advancement by lod_tier.
        // Kenshi RE: entity_id % 500 / % 252 spreads blend-eval cost across frames.
        // T0/T1 (lod 0-1): advance every frame.
        // T2    (lod 2):   advance every 2 frames (slot % 2 == frame_counter_ % 2).
        // T3+   (lod 3-4): skip entirely (static pose / offscreen).
        const uint32_t fc = frame_counter_;
        for (int i = 0; i < MAX_ANIMATED_NPC; ++i) {
            const uint32_t tier = states_[i].lod_tier;
            if (tier >= 3) continue;                           // T3+: no update
            if (tier == 2 && ((uint32_t)i % 2u != fc % 2u)) continue; // T2: every 2nd frame
            uint32_t cid = states_[i].clip_id;
            float dur = (cid < (uint32_t)clips_count_) ? clips_[cid].duration_s : 1.0f;
            states_[i].time_s += dt;
            if (states_[i].time_s >= dur) states_[i].time_s -= dur;
        }
    }

    void Upload() {
        // Write anim states into ring buffer's current slot (zero-copy via persistent map).
        void* dst = anim_state_ring_.MapWrite();
        if (dst) {
            memcpy(dst, states_, MAX_ANIMATED_NPC * sizeof(AnimNpcState));
            anim_state_ring_.Unmap();
        }
        anim_state_ring_.BindStorage(5);
        bones_ssbo_.Bind(4);
    }

    void BindSSBOs() {
        anim_state_ring_.BindStorage(5);
        bones_ssbo_.Bind(4);
    }

    // Dispatch skinning compute pass.
    // OpenGL: SSBOs 4+5 must already be bound (Upload() does this).
    // SDL_GPU: pass bindings with rw_buffers[0]=FinalBones, ro_buffers[0]=AnimState.
    //          bindings.cmd=nullptr is a safe no-op (Step 9 wires the SDL_GPUBuffer*).
    void DispatchSkinning(const GpuComputePass::StorageBindings& bindings = {}) {
        // skinning.comp uses local_size_x=1 (one NPC per invocation).
        static constexpr uint32_t SKIN_GROUPS = MAX_ANIMATED_NPC;
#ifdef MD_SDL_GPU
        if (bindings.cmd) {
            GpuComputePass pass;
            pass.Begin(&skin_pipeline_, bindings);
            pass.Dispatch(SKIN_GROUPS, 1u, 1u);
            pass.End();
        }
#endif
    }

    // Call once per frame AFTER all draw/compute that read anim_state_ring_.
    void AdvanceFrame() { anim_state_ring_.Advance(); ++frame_counter_; }

#ifdef MD_SDL_GPU
    // Upload anim state to SDL_GPU device buffer via a copy pass in cmd.
    // Call before DispatchSkinning(bindings) in the SDL_GPU frame sequence.
    void UploadSDLGPU(SDL_GPUCommandBuffer* cmd) {
        void* dst = anim_state_ring_.MapWriteSDL();
        if (dst) {
            memcpy(dst, states_, MAX_ANIMATED_NPC * sizeof(AnimNpcState));
            anim_state_ring_.UnmapSDL();
        }
        anim_state_ring_.Upload(cmd);
    }
    SDL_GPUBuffer* SDLBonesBuffer()    const { return bones_ssbo_.SDLBuffer(); }
    SDL_GPUBuffer* SDLAnimStateBuffer() const { return anim_state_ring_.SDLBuffer(); }
    // CPU-side bone matrices upload (bypasses skinning.comp for real-mesh animation).
    // data: float[npc_count * MAX_BONES * 16], laid out at slot offsets 0..npc_count-1
    void UploadBonesInCmd(SDL_GPUCommandBuffer* cmd, const void* data, int bytes, int byte_offset = 0) {
        if (bones_ssbo_.SDLBuffer()) bones_ssbo_.UploadInCmd(cmd, data, bytes, byte_offset);
    }

    // ── Step 3: GPU skinning data ─────────────────────────────────────────────
    SDL_GPUBuffer* SDLClipBuf()     const { return clip_buf_.SDLBuffer(); }
    SDL_GPUBuffer* SDLSkelBuf()     const { return skel_buf_.SDLBuffer(); }
    SDL_GPUBuffer* SDLNpcAnimBuf()  const { return npc_anim_ring_.SDLBuffer(); }

    // Upload clip + skeleton data once at startup (called from NpcRender after GLB loaded).
    // Uses SkinMesh GPU accessors (GpuTrackKF, GpuInvBind, etc.).
    bool UploadClipDataSDLGPU(SDL_GPUCommandBuffer* cmd,
                              const void* clip_headers, int ch_bytes,
                              const void* track_headers, int th_bytes,
                              const void* keyframes,    int kf_bytes,
                              const void* skel_data,    int sk_bytes) {
        if (!clip_headers || !skel_data) return false;
        const int total = ch_bytes + th_bytes + kf_bytes;
        if (!clip_buf_.SDLBuffer())
            clip_buf_.Init(total > 0 ? total : 4,
                           SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
        if (!skel_buf_.SDLBuffer())
            skel_buf_.Init(sk_bytes > 0 ? sk_bytes : 4,
                           SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
        // Upload as a single blob with headers at the front.
        alignas(16) static uint8_t s_tmp[4 * 1024 * 1024]; // 4 MB staging scratch, 16-byte aligned
        int offset = 0;
        auto append = [&](const void* src, int n) {
            if (src && n > 0 && (offset + n) <= (int)sizeof(s_tmp)) {
                memcpy(s_tmp + offset, src, n);
                offset += n;
            }
        };
        append(clip_headers, ch_bytes);
        append(track_headers, th_bytes);
        append(keyframes, kf_bytes);
        if (offset > 0) clip_buf_.UploadInCmd(cmd, s_tmp, offset);
        if (skel_data && sk_bytes > 0) skel_buf_.UploadInCmd(cmd, skel_data, sk_bytes);
        clip_data_ready_ = true;
        return true;
    }

    // Upload per-NPC anim state from AnimatorComponent data (replaces UploadBonesInCmd).
    void UploadNpcAnimSDLGPU(SDL_GPUCommandBuffer* cmd,
                             const NpcGpuAnim* data, int count) {
        if (!npc_anim_ring_.SDLBuffer())
            npc_anim_ring_.Init((uint32_t)(MAX_ANIMATED_NPC * (int)sizeof(NpcGpuAnim)), 5);
        void* dst = npc_anim_ring_.MapWriteSDL();
        if (dst) {
            memcpy(dst, data, count * (int)sizeof(NpcGpuAnim));
            npc_anim_ring_.UnmapSDL();
        }
        npc_anim_ring_.Upload(cmd);
    }

    bool ClipDataReady() const { return clip_data_ready_; }
#endif

    void Shutdown() {
        skin_pipeline_.Destroy();
        bones_ssbo_.Shutdown();
        anim_state_ring_.Shutdown();
        clip_buf_.Shutdown();
        skel_buf_.Shutdown();
        npc_anim_ring_.Shutdown();
    }

    // Accessors for editor panels (replaces direct field access — БОРГ-7/8).
    AnimNpcState&       GetState(int slot)       { return states_[slot]; }
    const AnimNpcState& GetState(int slot) const { return states_[slot]; }
    int                 ClipCount()        const { return clips_count_; }
    const AnimationClip& GetClip(int i)   const { return clips_[i]; }

private:
    AnimationSoA() = default;

    alignas(64) AnimNpcState  states_[MAX_ANIMATED_NPC];
    AnimationClip clips_[MAX_ANIM_CLIPS];
    int           clips_count_ = 0;

    SSBO               bones_ssbo_;      // compute output — regular SSBO
    GpuRingBuffer      anim_state_ring_; // CPU per-frame — ring-buffered
    GpuComputePipeline skin_pipeline_;   // skinning compute shader
    uint32_t           frame_counter_ = 0; // PERF-16: stagger index

    // Step 3: GPU skinning data buffers (static clip/skel + per-frame NPC state).
    SSBO          clip_buf_;           // clip headers + track headers + keyframes
    SSBO          skel_buf_;           // inv_bind + bind_pose + parent + process_order
    GpuRingBuffer npc_anim_ring_;      // per-NPC NpcGpuAnim (ring-buffered, per frame)
    bool          clip_data_ready_ = false;

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
