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
// ── Miniaudio path (M17) — delegates to AudioSystem singleton ────────────────
// MINIAUDIO_IMPLEMENTATION lives in audio_system.cpp (engine static lib).
// AudioHandle is now an opaque slot id; no miniaudio types in this header.
#  include <monkey_dust/audio/audio_system.h>

   struct AudioHandle {
       int  sfx_id = -1;
       bool valid  = false;
   };

   inline void audio_init()     { AudioSystem::Get().Init(); }
   inline void audio_shutdown() { AudioSystem::Get().Shutdown(); }

   inline AudioHandle audio_load(const char* path) {
       AudioHandle h;
       h.sfx_id = AudioSystem::Get().LoadSFX(path);
       h.valid  = h.sfx_id >= 0;
       return h;
   }
   inline void audio_play(AudioHandle& h) {
       if (h.valid) AudioSystem::Get().PlaySFX(h.sfx_id);
   }
   inline void audio_free(AudioHandle& h) {
       if (h.valid) { AudioSystem::Get().FreeSFX(h.sfx_id); h.valid = false; h.sfx_id = -1; }
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
