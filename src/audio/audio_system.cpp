// audio_system.cpp — the ONE translation unit that defines MINIAUDIO_IMPLEMENTATION.
// All other files include <monkey_dust/audio/audio_system.h> and stay miniaudio-free.
//
// MD_USE_LIBGODOT (Фаза C.5 Platform-шар, 2026-08-21): full-cutover
// backend replaces the entire miniaudio body below with Godot's
// AudioServer, same non-Node RID/Resource-based pattern as RenderingServer/
// Input/DisplayServer (AudioStreamPlayer, scene/audio/, would need
// SceneTree-membership -- deliberately avoided, verified via
// probes/libgodot_audio_test.cpp, be18c5b). AudioSystem's public class
// interface (audio_system.h) is UNCHANGED -- game/src call sites need
// zero changes, same model as window.h/input.h's port.
#ifdef MD_USE_LIBGODOT

#include <monkey_dust/audio/audio_system.h>
#include <monkey_dust/platform/md_log.h>
#include "servers/audio/audio_server.h"
#include "scene/resources/audio_stream_wav.h"
#include "core/math/math_funcs.h"
#include <unistd.h>
#include <climits>
#include <string>

// ── File-static Godot audio storage (mirrors the miniaudio s_sfx/s_music/
// s_ambience pattern below exactly -- AudioSystem class itself carries no
// Godot types, keeping audio_system.h's "no ma_*/no Godot types exposed"
// discipline symmetric between both backends). ──────────────────────────────
static Ref<AudioStream>          s_sfx_stream[AudioSystem::MAX_SFX];
static Ref<AudioStreamPlayback>  s_music_playback;
static Ref<AudioStreamPlayback>  s_ambience_playback;

// Godot's FileAccess does NOT resolve bare relative paths via cwd the way
// stdio/miniaudio does -- found live (probes/libgodot_audio_test.cpp first
// run: "Can't open file from path 'data/audio/hit.wav'"). Existing game/src
// callers pass cwd-relative paths (e.g. "data/audio/hit.wav") unchanged
// from the miniaudio path's convention, so resolve to absolute here rather
// than push a behavior change onto every call site.
//
// MUST capture cwd via a NAMESPACE-SCOPE static (real static-initialization,
// runs before main() -- NOT a function-local "static" Meyer's-singleton,
// which only initializes on first CALL and would run AFTER
// GodotInstance::start() here since LoadSFX/PlayMusic/etc are called after
// start(), capturing the wrong directory). Found live via strace
// (probes/libgodot_audio_system_test.cpp first run, before this fix used a
// function-local static: silently resolved to ".../build_libgodot/
// data/audio/hit.wav" instead of the repo root): `strace -f -e trace=chdir`
// showed Godot's own startup repeatedly calling chdir() into the
// executable's own directory during instance->start()/iteration(), so any
// getcwd() called after start() reflects Godot's chdir target, not the
// process's real launch-time cwd.
// std::string, NOT Godot String -- a global Godot String risks depending
// on Memory::alloc_static (Godot's own allocator), which isn't initialized
// this early (before Main::setup() runs inside instance->start(), itself
// called from main() well after this TU's static-init phase). std::string
// only needs the C++ runtime, safe at this point; converted to Godot
// String at call time in ResolveAudioPath() (post-init, safe).
static const std::string s_launch_cwd = []() -> std::string {
    char buf[PATH_MAX];
    return getcwd(buf, sizeof(buf)) ? std::string(buf) : std::string(".");
}();

static String ResolveAudioPath(const char* path) {
    if (path[0] == '/') return String(path);  // already absolute
    return String((s_launch_cwd + "/" + path).c_str());
}

// AudioServer::start_playback_stream()'s volume vector MUST be exactly
// MAX_CHANNELS_PER_BUS=4 elements -- found live (probes/libgodot_audio_
// test.cpp first run: ERR_FAIL at audio_server.cpp:1245, 1-element vector
// silently failed the call). volume is linear [0..1], converted once here
// -- every PlaySFX/PlayMusic/PlayAmbience call site keeps its existing
// linear-volume public API.
static Vector<AudioFrame> MakeVolumeVector(float volume) {
    float db = Math::linear_to_db(volume);
    Vector<AudioFrame> v;
    for (int i = 0; i < 4; ++i) v.push_back(AudioFrame(db, db));
    return v;
}

AudioSystem& AudioSystem::Get() {
    static AudioSystem inst;
    return inst;
}

bool AudioSystem::Init() {
    if (ready_) return true;
    if (!AudioServer::get_singleton()) {
        MD_LOG(MD_LOG_WARNING, "[AudioSystem] AudioServer::get_singleton() is null (GodotInstance not started?)");
        return false;
    }
    ready_ = true;
    MD_LOG(MD_LOG_INFO, "[AudioSystem] Init OK (LibGodot AudioServer backend)");
    return true;
}

void AudioSystem::Shutdown() {
    if (!ready_) return;
    StopMusic();
    StopAmbience();
    for (int i = 0; i < MAX_SFX; ++i) FreeSFX(i);
    ready_ = false;
}

// ── SFX pool ──────────────────────────────────────────────────────────────────

int AudioSystem::LoadSFX(const char* path) {
    if (!ready_ || !path) return -1;
    for (int i = 0; i < MAX_SFX; ++i) {
        if (sfx_in_use_[i]) continue;
        Dictionary opts;
        Ref<AudioStreamWAV> stream = AudioStreamWAV::load_from_file(ResolveAudioPath(path), opts);
        if (stream.is_null()) {
            MD_LOG(MD_LOG_WARNING, "[AudioSystem] LoadSFX '%s' failed", path);
            return -1;
        }
        s_sfx_stream[i] = stream;
        sfx_in_use_[i] = true;
        return i;
    }
    MD_LOG(MD_LOG_WARNING, "[AudioSystem] SFX pool full (MAX=%d)", MAX_SFX);
    return -1;
}

void AudioSystem::PlaySFX(int sfx_id, float volume) {
    if (!ready_ || sfx_id < 0 || sfx_id >= MAX_SFX || !sfx_in_use_[sfx_id]) return;
    // Fresh instantiate_playback() per call (not seek+restart of a shared
    // instance, unlike the miniaudio path) -- AudioServer's internal
    // AudioStreamPlaybackListNode holds its own Ref, so overlapping PlaySFX
    // calls on the same slot naturally polyphony instead of cutting each
    // other off. Verified mechanism (start_playback_stream keeps the Ref
    // alive internally): probes/libgodot_audio_test.cpp.
    Ref<AudioStreamPlayback> playback = s_sfx_stream[sfx_id]->instantiate_playback();
    if (playback.is_null()) return;
    AudioServer::get_singleton()->start_playback_stream(playback, "Master", MakeVolumeVector(volume));
}

void AudioSystem::FreeSFX(int sfx_id) {
    if (sfx_id < 0 || sfx_id >= MAX_SFX || !sfx_in_use_[sfx_id]) return;
    s_sfx_stream[sfx_id] = Ref<AudioStream>();
    sfx_in_use_[sfx_id] = false;
}

void AudioSystem::PlaySFXOnce(const char* path, float volume) {
    if (!ready_ || !path) return;
    Dictionary opts;
    Ref<AudioStreamWAV> stream = AudioStreamWAV::load_from_file(ResolveAudioPath(path), opts);
    if (stream.is_null()) {
        MD_LOG(MD_LOG_WARNING, "[AudioSystem] PlaySFXOnce '%s' failed", path);
        return;
    }
    Ref<AudioStreamPlayback> playback = stream->instantiate_playback();
    if (playback.is_null()) return;
    AudioServer::get_singleton()->start_playback_stream(playback, "Master", MakeVolumeVector(volume));
    // stream/playback local Refs go out of scope here -- safe, AudioServer's
    // playback_node holds its own Ref (verified probes/libgodot_audio_test.cpp).
}

// ── Music ─────────────────────────────────────────────────────────────────────

void AudioSystem::PlayMusic(const char* path, float volume) {
    if (!ready_ || !path) return;
    StopMusic();
    Dictionary opts;
    Ref<AudioStreamWAV> stream = AudioStreamWAV::load_from_file(ResolveAudioPath(path), opts);
    if (stream.is_null()) {
        MD_LOG(MD_LOG_WARNING, "[AudioSystem] PlayMusic '%s' failed", path);
        return;
    }
    stream->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
    s_music_playback = stream->instantiate_playback();
    if (s_music_playback.is_null()) return;
    AudioServer::get_singleton()->start_playback_stream(s_music_playback, "Master", MakeVolumeVector(volume));
    music_loaded_ = true;
    MD_LOG(MD_LOG_INFO, "[AudioSystem] Music: %s", path);
}

void AudioSystem::StopMusic() {
    if (!music_loaded_) return;
    AudioServer::get_singleton()->stop_playback_stream(s_music_playback);
    s_music_playback = Ref<AudioStreamPlayback>();
    music_loaded_ = false;
}

void AudioSystem::SetMusicVolume(float volume) {
    if (!music_loaded_) return;
    AudioServer::get_singleton()->set_playback_bus_exclusive(s_music_playback, "Master", MakeVolumeVector(volume));
}

bool AudioSystem::IsMusicPlaying() const {
    return music_loaded_ && AudioServer::get_singleton()->is_playback_active(s_music_playback);
}

// ── Ambience ──────────────────────────────────────────────────────────────────

void AudioSystem::PlayAmbience(const char* path, float volume) {
    if (!ready_ || !path) return;
    StopAmbience();
    Dictionary opts;
    Ref<AudioStreamWAV> stream = AudioStreamWAV::load_from_file(ResolveAudioPath(path), opts);
    if (stream.is_null()) {
        MD_LOG(MD_LOG_WARNING, "[AudioSystem] PlayAmbience '%s' failed", path);
        return;
    }
    stream->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
    s_ambience_playback = stream->instantiate_playback();
    if (s_ambience_playback.is_null()) return;
    AudioServer::get_singleton()->start_playback_stream(s_ambience_playback, "Master", MakeVolumeVector(volume));
    ambience_loaded_ = true;
    MD_LOG(MD_LOG_INFO, "[AudioSystem] Ambience: %s", path);
}

void AudioSystem::StopAmbience() {
    if (!ambience_loaded_) return;
    AudioServer::get_singleton()->stop_playback_stream(s_ambience_playback);
    s_ambience_playback = Ref<AudioStreamPlayback>();
    ambience_loaded_ = false;
}

void AudioSystem::SetAmbienceVolume(float volume) {
    if (!ambience_loaded_) return;
    AudioServer::get_singleton()->set_playback_bus_exclusive(s_ambience_playback, "Master", MakeVolumeVector(volume));
}

// ── Master volume ─────────────────────────────────────────────────────────────

void AudioSystem::SetMasterVolume(float volume) {
    master_volume_ = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (!ready_) return;
    AudioServer* as = AudioServer::get_singleton();
    int idx = as->get_bus_index("Master");
    as->set_bus_volume_db(idx, Math::linear_to_db(master_volume_));
}

#else // !MD_USE_LIBGODOT -- existing miniaudio path, unchanged.

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

#endif // MD_USE_LIBGODOT
