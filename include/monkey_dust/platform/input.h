#pragma once

// Platform input abstraction.
// Default: Raylib (drop-in, no behaviour change).
// M1 migration: compile with -DUSE_SDL3 to enable SDL3 input path.
// M3 migration: Raylib built with PLATFORM=SDL — SDL3 window replaces GLFW.
//               Raylib's EndDrawing() pumps SDL events; our EventWatcher intercepts
//               them into a double-buffer so input_begin_frame() sees "just pressed".
// Rule M-A: only Main.cpp and EditorMain.cpp may include this header.

#ifndef USE_SDL3
// ── Raylib path (default) ─────────────────────────────────────────────────────
#  include "raylib.h"
   inline void  input_init()                         {}  // no-op
   inline bool  input_key_down    (int key) { return IsKeyDown(key); }
   inline bool  input_key_pressed (int key) { return IsKeyPressed(key); }
   inline bool  input_mouse_pressed(int btn){ return IsMouseButtonPressed(btn); }
   inline float input_mouse_x()             { return GetMousePosition().x; }
   inline float input_mouse_y()             { return GetMousePosition().y; }
   inline void  input_begin_frame()         {}  // no-op: Raylib pumps events in EndDrawing
   inline bool  input_should_quit()         { return WindowShouldClose(); }

#else
// ── SDL3 path (M1+M3) ────────────────────────────────────────────────────────
// Raylib is built with PLATFORM=SDL so SDL3 owns the window.
// Raylib's EndDrawing() calls SDL_PollEvent() internally.
// We register an EventWatcher that fires during that pump and fills _next[].
// input_begin_frame() swaps _next → current so the game reads the correct state.
#  include <SDL3/SDL.h>
#  include <cstring>

   // Key mapping: Raylib KEY_* constants → SDL3 SDL_Scancode values.
   // Only constants used in Main.cpp are listed; extend as needed.
#  define KEY_A             SDL_SCANCODE_A
#  define KEY_B             SDL_SCANCODE_B
#  define KEY_D             SDL_SCANCODE_D
#  define KEY_E             SDL_SCANCODE_E
#  define KEY_F             SDL_SCANCODE_F
#  define KEY_R             SDL_SCANCODE_R
#  define KEY_S             SDL_SCANCODE_S
#  define KEY_W             SDL_SCANCODE_W
#  define KEY_X             SDL_SCANCODE_X
#  define KEY_Y             SDL_SCANCODE_Y
#  define KEY_Z             SDL_SCANCODE_Z
#  define KEY_ONE           SDL_SCANCODE_1
#  define KEY_TWO           SDL_SCANCODE_2
#  define KEY_THREE         SDL_SCANCODE_3
#  define KEY_FOUR          SDL_SCANCODE_4
#  define KEY_ESCAPE        SDL_SCANCODE_ESCAPE
#  define KEY_TAB           SDL_SCANCODE_TAB
#  define KEY_F1            SDL_SCANCODE_F1
#  define KEY_F2            SDL_SCANCODE_F2
#  define KEY_F3            SDL_SCANCODE_F3
#  define KEY_F4            SDL_SCANCODE_F4
#  define KEY_F5            SDL_SCANCODE_F5
#  define KEY_F6            SDL_SCANCODE_F6
#  define KEY_F7            SDL_SCANCODE_F7
#  define KEY_F8            SDL_SCANCODE_F8
#  define KEY_F9            SDL_SCANCODE_F9
#  define KEY_F10           SDL_SCANCODE_F10
#  define KEY_LEFT_CONTROL  SDL_SCANCODE_LCTRL
#  define KEY_RIGHT_CONTROL SDL_SCANCODE_RCTRL
#  define KEY_LEFT_SHIFT    SDL_SCANCODE_LSHIFT
#  define KEY_RIGHT_SHIFT   SDL_SCANCODE_RSHIFT
#  define KEY_LEFT          SDL_SCANCODE_LEFT
#  define KEY_RIGHT         SDL_SCANCODE_RIGHT
#  define KEY_UP            SDL_SCANCODE_UP
#  define KEY_DOWN          SDL_SCANCODE_DOWN
   // Mouse buttons: Raylib 0-based → SDL3 1-based macros
#  define MOUSE_BUTTON_LEFT   SDL_BUTTON_LEFT
#  define MOUSE_BUTTON_RIGHT  SDL_BUTTON_RIGHT
#  define MOUSE_BUTTON_MIDDLE SDL_BUTTON_MIDDLE

   // Double-buffered "just pressed" state.
   // _next[] is filled by EventWatcher during Raylib's SDL_PollEvent pump.
   // input_begin_frame() swaps _next → s_keys, then clears _next.
   namespace _sdl3_input {
       inline bool  s_keys [SDL_SCANCODE_COUNT] = {};  // current frame (game reads)
       inline bool  s_next [SDL_SCANCODE_COUNT] = {};  // filled by EventWatcher
       inline bool  s_mouse[6]                  = {};
       inline bool  s_mnext[6]                  = {};
       inline bool  s_quit                      = false;
       inline float s_scroll_y                  = 0.f;  // accumulated per frame
       inline float s_scroll_accum              = 0.f;  // written by watcher
   }

   // SDL event watcher — registered once at input_init().
   // Fires synchronously during each SDL_PollEvent call (in Raylib's EndDrawing).
   // Returns 0: non-consuming, Raylib also sees the event.
   inline bool _sdl3_event_watcher(void*, SDL_Event* ev) {
       if (ev->type == SDL_EVENT_QUIT)
           _sdl3_input::s_quit = true;
       if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
           int sc = (int)ev->key.scancode;
           if (sc >= 0 && sc < SDL_SCANCODE_COUNT)
               _sdl3_input::s_next[sc] = true;
       }
       if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
           int b = (int)ev->button.button;
           if (b > 0 && b < 6) _sdl3_input::s_mnext[b] = true;
       }
       if (ev->type == SDL_EVENT_MOUSE_WHEEL)
           _sdl3_input::s_scroll_accum += ev->wheel.y;
       return true;  // non-consuming: Raylib also gets the event
   }

   // Call once after InitWindow() (Raylib's SDL3 backend is already initialised).
   inline void input_init() {
       SDL_AddEventWatch(_sdl3_event_watcher, nullptr);
   }

   // Call at the START of each frame: swap _next → current, clear _next.
   // Events were filled by the EventWatcher during the PREVIOUS frame's EndDrawing.
   inline void input_begin_frame() {
       memcpy(_sdl3_input::s_keys,  _sdl3_input::s_next,  sizeof(_sdl3_input::s_keys));
       memcpy(_sdl3_input::s_mouse, _sdl3_input::s_mnext, sizeof(_sdl3_input::s_mouse));
       memset(_sdl3_input::s_next,  0, sizeof(_sdl3_input::s_next));
       memset(_sdl3_input::s_mnext, 0, sizeof(_sdl3_input::s_mnext));
       _sdl3_input::s_scroll_y     = _sdl3_input::s_scroll_accum;
       _sdl3_input::s_scroll_accum = 0.f;
   }

   inline bool input_key_down(int key) {
       int n = 0; const bool* st = SDL_GetKeyboardState(&n);
       return (key >= 0 && key < n) ? st[key] : false;
   }
   inline bool input_key_pressed(int key) {
       return (key >= 0 && key < SDL_SCANCODE_COUNT)
              ? _sdl3_input::s_keys[key] : false;
   }
   inline bool  input_mouse_pressed(int btn) {
       return (btn > 0 && btn < 6) ? _sdl3_input::s_mouse[btn] : false;
   }
   inline float input_mouse_x()       { float x=0; SDL_GetMouseState(&x, nullptr); return x; }
   inline float input_mouse_y()       { float y=0; SDL_GetMouseState(nullptr, &y); return y; }
   inline float input_get_scroll_y()  { return _sdl3_input::s_scroll_y; }
   inline bool  input_should_quit()   { return _sdl3_input::s_quit; }

#endif // USE_SDL3
