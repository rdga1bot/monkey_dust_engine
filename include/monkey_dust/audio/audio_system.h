#pragma once
// AudioSystem — full miniaudio integration for music, ambience, and SFX.
// MINIAUDIO_IMPLEMENTATION lives exclusively in audio_system.cpp.
// All miniaudio types are hidden behind this header (no ma_* exposed).
//
// SFX pool: max MAX_SFX pre-loaded one-shot sounds (BSS storage).
// Music / Ambience: one streaming track each, looping.
// Fire-and-forget: PlaySFXOnce(path) for Lua-triggered sounds.

class AudioSystem {
public:
    static AudioSystem& Get();

    bool Init();
    void Shutdown();
    bool IsReady() const { return ready_; }

    // ── Pre-loaded SFX pool ─────────────────────────────────────────────────
    static constexpr int MAX_SFX = 32;

    // Load SFX from file into pool slot. Returns slot id [0..MAX_SFX-1] or -1.
    int  LoadSFX (const char* path);
    // Play pre-loaded SFX slot at given volume [0..1].
    void PlaySFX (int sfx_id, float volume = 1.0f);
    void FreeSFX (int sfx_id);

    // Fire-and-forget: load + play transiently (no pool slot consumed).
    void PlaySFXOnce(const char* path, float volume = 1.0f);

    // ── Music (streaming, looping) ──────────────────────────────────────────
    void PlayMusic (const char* path, float volume = 1.0f);
    void StopMusic ();
    void SetMusicVolume(float volume);
    bool IsMusicPlaying() const;

    // ── Ambience (streaming, looping) ───────────────────────────────────────
    void PlayAmbience (const char* path, float volume = 0.4f);
    void StopAmbience ();
    void SetAmbienceVolume(float volume);

    // ── Master volume [0..1] ────────────────────────────────────────────────
    void  SetMasterVolume(float volume);
    float GetMasterVolume() const { return master_volume_; }

private:
    AudioSystem() = default;

    bool  ready_          = false;
    float master_volume_  = 1.0f;
    bool  sfx_in_use_[MAX_SFX] = {};
    bool  music_loaded_    = false;
    bool  ambience_loaded_ = false;
};
