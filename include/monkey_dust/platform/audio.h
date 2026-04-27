#pragma once

// Platform audio abstraction.
// Default: Raylib (Sound / LoadSound / PlaySound).
// M5 migration: compile with -DUSE_SDL3 to use miniaudio directly.
// Rule M-A: only Main.cpp may include this header.

#ifndef USE_SDL3
// ── Raylib audio path ────────────────────────────────────────────────────────
#  include "raylib.h"

   struct AudioHandle {
       Sound snd;
       bool  valid = false;
   };

   inline void audio_init()                    { InitAudioDevice(); }
   inline void audio_shutdown()                { CloseAudioDevice(); }

   inline AudioHandle audio_load(const char* path) {
       AudioHandle h;
       h.snd   = LoadSound(path);
       h.valid = IsSoundValid(h.snd);
       return h;
   }
   inline void audio_play(AudioHandle& h)      { if (h.valid) PlaySound(h.snd); }
   inline void audio_free(AudioHandle& h)      { if (h.valid) { UnloadSound(h.snd); h.valid = false; } }

#else
// ── Miniaudio path (M5) ──────────────────────────────────────────────────────
// Miniaudio is bundled with Raylib at third_party/raylib/src/external/miniaudio.h.
// MINIAUDIO_IMPLEMENTATION must be defined in exactly one TU — only Main.cpp
// includes this header (Rule M-A), so this is safe.
#  define MINIAUDIO_IMPLEMENTATION
#  include "external/miniaudio.h"
#  include <cstdio>

   namespace _ma {
       inline ma_engine& engine() { static ma_engine e; return e; }
       inline bool&      ready()  { static bool r = false; return r; }
   }

   struct AudioHandle {
       ma_sound snd;
       bool     valid = false;
   };

   inline void audio_init() {
       ma_result r = ma_engine_init(nullptr, &_ma::engine());
       if (r != MA_SUCCESS)
           fprintf(stderr, "[audio] ma_engine_init failed: %d\n", r);
       else
           _ma::ready() = true;
   }

   inline void audio_shutdown() {
       if (_ma::ready()) { ma_engine_uninit(&_ma::engine()); _ma::ready() = false; }
   }

   inline AudioHandle audio_load(const char* path) {
       AudioHandle h;
       if (!_ma::ready()) return h;
       FILE* f = fopen(path, "rb");
       if (!f) { fprintf(stderr, "[audio] file not found '%s'\n", path); return h; }
       fclose(f);
       ma_result r = ma_sound_init_from_file(&_ma::engine(), path, 0, nullptr, nullptr, &h.snd);
       if (r != MA_SUCCESS)
           fprintf(stderr, "[audio] failed to load '%s': %d\n", path, r);
       else
           h.valid = true;
       return h;
   }

   inline void audio_play(AudioHandle& h) {
       if (!h.valid) return;
       ma_sound_seek_to_pcm_frame(&h.snd, 0);
       ma_sound_start(&h.snd);
   }

   inline void audio_free(AudioHandle& h) {
       if (h.valid) { ma_sound_uninit(&h.snd); h.valid = false; }
   }

#endif // USE_SDL3

// ── Frame timing abstraction ─────────────────────────────────────────────────
// Under USE_SDL3, SDL_GetTicks() provides delta; else Raylib GetFrameTime().

#ifndef USE_SDL3
   inline void  frame_tick() {}  // no-op: Raylib tracks time in EndDrawing
   inline float frame_dt()  { return GetFrameTime(); }
   inline int   frame_fps() { return GetFPS(); }
#else
#  include <SDL3/SDL.h>
   namespace _frame {
       inline Uint64& prev() { static Uint64 t = 0; return t; }
       inline float&  dt()   { static float  d = 0.f; return d; }
   }
   // Call ONCE per frame at the START (before game logic).
   // input_begin_frame() is called first; call frame_tick() right after.
   inline void frame_tick() {
       Uint64 now = SDL_GetTicks();
       if (_frame::prev() == 0) _frame::prev() = now;
       _frame::dt() = (float)(now - _frame::prev()) * 0.001f;
       if (_frame::dt() > 0.1f) _frame::dt() = 0.1f;  // cap at 100ms
       _frame::prev() = now;
   }
   inline float frame_dt()  { return _frame::dt(); }
   inline int   frame_fps() { return (_frame::dt() > 0.f) ? (int)(1.f / _frame::dt()) : 0; }
#endif
