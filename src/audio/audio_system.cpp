// audio_system.cpp — the ONE translation unit that defines MINIAUDIO_IMPLEMENTATION.
// All other files include <monkey_dust/audio/audio_system.h> and stay miniaudio-free.
#define MINIAUDIO_IMPLEMENTATION
#include "external/miniaudio.h"

#include <monkey_dust/audio/audio_system.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

// ── File-static miniaudio storage (BSS, no heap) ─────────────────────────────
static ma_engine s_engine;
static ma_sound  s_sfx[AudioSystem::MAX_SFX];
static ma_sound  s_music;
static ma_sound  s_ambience;

// ── Singleton ─────────────────────────────────────────────────────────────────

AudioSystem& AudioSystem::Get() {
    static AudioSystem inst;
    return inst;
}

bool AudioSystem::Init() {
    if (ready_) return true;
    ma_result r = ma_engine_init(nullptr, &s_engine);
    if (r != MA_SUCCESS) {
        MD_LOG(MD_LOG_WARNING, "[AudioSystem] ma_engine_init failed: %d", (int)r);
        return false;
    }
    ready_ = true;
    MD_LOG(MD_LOG_INFO, "[AudioSystem] Init OK");
    return true;
}

void AudioSystem::Shutdown() {
    if (!ready_) return;
    StopMusic();
    StopAmbience();
    for (int i = 0; i < MAX_SFX; ++i) FreeSFX(i);
    ma_engine_uninit(&s_engine);
    ready_ = false;
}

// ── SFX pool ──────────────────────────────────────────────────────────────────

int AudioSystem::LoadSFX(const char* path) {
    if (!ready_ || !path) return -1;
    for (int i = 0; i < MAX_SFX; ++i) {
        if (sfx_in_use_[i]) continue;
        ma_result r = ma_sound_init_from_file(&s_engine, path,
                          MA_SOUND_FLAG_NO_SPATIALIZATION,
                          nullptr, nullptr, &s_sfx[i]);
        if (r != MA_SUCCESS) {
            MD_LOG(MD_LOG_WARNING, "[AudioSystem] LoadSFX '%s' failed: %d", path, (int)r);
            return -1;
        }
        sfx_in_use_[i] = true;
        return i;
    }
    MD_LOG(MD_LOG_WARNING, "[AudioSystem] SFX pool full (MAX=%d)", MAX_SFX);
    return -1;
}

void AudioSystem::PlaySFX(int sfx_id, float volume) {
    if (!ready_ || sfx_id < 0 || sfx_id >= MAX_SFX || !sfx_in_use_[sfx_id]) return;
    ma_sound_set_volume(&s_sfx[sfx_id], volume);
    ma_sound_seek_to_pcm_frame(&s_sfx[sfx_id], 0);
    ma_sound_start(&s_sfx[sfx_id]);
}

void AudioSystem::FreeSFX(int sfx_id) {
    if (sfx_id < 0 || sfx_id >= MAX_SFX || !sfx_in_use_[sfx_id]) return;
    ma_sound_uninit(&s_sfx[sfx_id]);
    sfx_in_use_[sfx_id] = false;
}

void AudioSystem::PlaySFXOnce(const char* path, float volume) {
    if (!ready_ || !path) return;
    // ma_engine_play_sound: engine manages lifetime automatically.
    ma_engine_play_sound(&s_engine, path, nullptr);
    (void)volume;  // ma_engine_play_sound has no per-call volume; use master
}

// ── Music ─────────────────────────────────────────────────────────────────────

void AudioSystem::PlayMusic(const char* path, float volume) {
    if (!ready_ || !path) return;
    StopMusic();
    ma_result r = ma_sound_init_from_file(&s_engine, path,
                      MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
                      nullptr, nullptr, &s_music);
    if (r != MA_SUCCESS) {
        MD_LOG(MD_LOG_WARNING, "[AudioSystem] PlayMusic '%s' failed: %d", path, (int)r);
        return;
    }
    ma_sound_set_looping(&s_music, MA_TRUE);
    ma_sound_set_volume(&s_music, volume);
    ma_sound_start(&s_music);
    music_loaded_ = true;
    MD_LOG(MD_LOG_INFO, "[AudioSystem] Music: %s", path);
}

void AudioSystem::StopMusic() {
    if (!music_loaded_) return;
    ma_sound_stop(&s_music);
    ma_sound_uninit(&s_music);
    music_loaded_ = false;
}

void AudioSystem::SetMusicVolume(float volume) {
    if (music_loaded_) ma_sound_set_volume(&s_music, volume);
}

bool AudioSystem::IsMusicPlaying() const {
    return music_loaded_ && (bool)ma_sound_is_playing(&s_music);
}

// ── Ambience ──────────────────────────────────────────────────────────────────

void AudioSystem::PlayAmbience(const char* path, float volume) {
    if (!ready_ || !path) return;
    StopAmbience();
    ma_result r = ma_sound_init_from_file(&s_engine, path,
                      MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
                      nullptr, nullptr, &s_ambience);
    if (r != MA_SUCCESS) {
        MD_LOG(MD_LOG_WARNING, "[AudioSystem] PlayAmbience '%s' failed: %d", path, (int)r);
        return;
    }
    ma_sound_set_looping(&s_ambience, MA_TRUE);
    ma_sound_set_volume(&s_ambience, volume);
    ma_sound_start(&s_ambience);
    ambience_loaded_ = true;
    MD_LOG(MD_LOG_INFO, "[AudioSystem] Ambience: %s", path);
}

void AudioSystem::StopAmbience() {
    if (!ambience_loaded_) return;
    ma_sound_stop(&s_ambience);
    ma_sound_uninit(&s_ambience);
    ambience_loaded_ = false;
}

void AudioSystem::SetAmbienceVolume(float volume) {
    if (ambience_loaded_) ma_sound_set_volume(&s_ambience, volume);
}

// ── Master volume ─────────────────────────────────────────────────────────────

void AudioSystem::SetMasterVolume(float volume) {
    master_volume_ = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (ready_) ma_engine_set_volume(&s_engine, master_volume_);
}
