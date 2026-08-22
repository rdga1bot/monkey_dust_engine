#pragma once

// Platform input abstraction.
// M1 migration (compile with -DUSE_SDL3) made this the only path; the Raylib
// fallback was removed 2026-08-09 (USE_SDL3=ON has been the only buildable
// configuration, see engine/CMakeLists.txt's FATAL_ERROR gate).
// Rule M-A: only Main.cpp and EditorMain.cpp may include this header.
//
// MD_USE_LIBGODOT (Фаза C.5 Platform-шар, 2026-08-20): full-cutover
// backend -- SDL3 not needed at all when this path is active (window,
// input AND audio all replaced by Godot-native systems; see
// docs/LIBGODOT_MIGRATION_PLAN_PHASE_A_F.md's "Platform-шар" section).
// Same public API as the SDL3 path below (KEY_* values, input_key_
// pressed/down/mouse_x/y/etc signatures) -- game/src call sites need
// ZERO changes, only this header's internals differ, same model as the
// canonical §3.1 abstractions (MdCamera/Texture/Shader/Mesh, Фаза C).
#ifdef MD_USE_LIBGODOT

#  include "core/input/input.h"
#  include "core/input/input_event.h"
#  include "core/os/keyboard.h"
#  include <cstring>

   // game/src passes SDL_Scancode values directly (both via the KEY_*
   // macros below AND raw SDL_SCANCODE_* identifiers at many call sites
   // -- confirmed via grep of game/src/main.cpp, e.g.
   // input_key_pressed(SDL_SCANCODE_F3)), so KEY_* here MUST still
   // resolve to SDL_Scancode values, not Godot Key values directly --
   // the translation happens INSIDE input_key_down/pressed below via
   // kScancodeToGodotKey, not via these macros.
#  include <SDL3/SDL_scancode.h>
#  define KEY_A             SDL_SCANCODE_A
#  define KEY_B             SDL_SCANCODE_B
#  define KEY_D             SDL_SCANCODE_D
#  define KEY_E             SDL_SCANCODE_E
#  define KEY_F             SDL_SCANCODE_F
#  define KEY_M             SDL_SCANCODE_M
#  define KEY_R             SDL_SCANCODE_R
#  define KEY_S             SDL_SCANCODE_S
#  define KEY_T             SDL_SCANCODE_T
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
#  define KEY_ENTER         SDL_SCANCODE_RETURN
#  define KEY_SPACE         SDL_SCANCODE_SPACE
#  define KEY_BACKSPACE     SDL_SCANCODE_BACKSPACE
   // Godot's MouseButton enum (core/input/input_enums.h): LEFT=1,
   // RIGHT=2, MIDDLE=3 -- DIFFERENT order than SDL3's SDL_BUTTON_LEFT=1/
   // MIDDLE=2/RIGHT=3. input_mouse_down/pressed cast this value directly
   // to (MouseButton), so these MUST match Godot's real enum values, not
   // SDL's convention -- verified from real header, not assumed.
#  define MOUSE_BUTTON_LEFT   1
#  define MOUSE_BUTTON_RIGHT  2
#  define MOUSE_BUTTON_MIDDLE 3

   // SDL_Scancode -> Godot Key (PHYSICAL position, matches SDL_Scancode's
   // own semantics) -- verified live probes/libgodot_input_map_test.cpp
   // (c36d05d, 27/27 round-tripped via synthetic Input::parse_input_event()
   // + flush_buffered_events() + is_physical_key_pressed() readback).
   // LCTRL/RCTRL and LSHIFT/RSHIFT both map to one Godot Key (no
   // left/right distinction in the base Key enum) -- input.h's own
   // call sites never check the right-side variant, so no behavior lost.
   namespace _godot_input {
       inline Key*& scancode_table() {
           static Key table[SDL_SCANCODE_COUNT];
           static bool init = false;
           if (!init) {
               for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) table[i] = Key::NONE;
               table[SDL_SCANCODE_A] = Key::A;             table[SDL_SCANCODE_B] = Key::B;
               table[SDL_SCANCODE_D] = Key::D;             table[SDL_SCANCODE_E] = Key::E;
               table[SDL_SCANCODE_F] = Key::F;             table[SDL_SCANCODE_M] = Key::M;
               table[SDL_SCANCODE_R] = Key::R;             table[SDL_SCANCODE_S] = Key::S;
               table[SDL_SCANCODE_T] = Key::T;             table[SDL_SCANCODE_W] = Key::W;
               table[SDL_SCANCODE_X] = Key::X;             table[SDL_SCANCODE_Y] = Key::Y;
               table[SDL_SCANCODE_Z] = Key::Z;
               table[SDL_SCANCODE_1] = Key::KEY_1;         table[SDL_SCANCODE_2] = Key::KEY_2;
               table[SDL_SCANCODE_3] = Key::KEY_3;         table[SDL_SCANCODE_4] = Key::KEY_4;
               table[SDL_SCANCODE_ESCAPE] = Key::ESCAPE;   table[SDL_SCANCODE_TAB] = Key::TAB;
               table[SDL_SCANCODE_F1]  = Key::F1;          table[SDL_SCANCODE_F2]  = Key::F2;
               table[SDL_SCANCODE_F3]  = Key::F3;          table[SDL_SCANCODE_F4]  = Key::F4;
               table[SDL_SCANCODE_F5]  = Key::F5;          table[SDL_SCANCODE_F6]  = Key::F6;
               table[SDL_SCANCODE_F7]  = Key::F7;          table[SDL_SCANCODE_F8]  = Key::F8;
               table[SDL_SCANCODE_F9]  = Key::F9;          table[SDL_SCANCODE_F10] = Key::F10;
               table[SDL_SCANCODE_LCTRL]  = Key::CTRL;     table[SDL_SCANCODE_RCTRL]  = Key::CTRL;
               table[SDL_SCANCODE_LSHIFT] = Key::SHIFT;    table[SDL_SCANCODE_RSHIFT] = Key::SHIFT;
               table[SDL_SCANCODE_LEFT]  = Key::LEFT;      table[SDL_SCANCODE_RIGHT] = Key::RIGHT;
               table[SDL_SCANCODE_UP]    = Key::UP;        table[SDL_SCANCODE_DOWN]  = Key::DOWN;
               table[SDL_SCANCODE_RETURN] = Key::ENTER;    table[SDL_SCANCODE_SPACE] = Key::SPACE;
               table[SDL_SCANCODE_BACKSPACE] = Key::BACKSPACE;
               init = true;
           }
           static Key* ptr = table;
           return ptr;
       }
       // "Just pressed" edge state -- Input singleton only exposes
       // held-state (is_physical_key_pressed), so edge detection is done
       // here the same way the SDL3 path's s_next/s_keys swap already
       // works: input_begin_frame() diffs this-frame held-state against
       // last-frame's snapshot.
       inline bool s_down_prev[SDL_SCANCODE_COUNT] = {};
       inline bool s_pressed_this_frame[SDL_SCANCODE_COUNT] = {};
       inline bool s_mouse_down_prev[6] = {};
       inline bool s_mouse_pressed_this_frame[6] = {};
       inline bool s_quit = false;
   }

   inline void input_init() {
       // Input singleton state updates automatically from real OS events
       // once the GodotInstance is ticking with a live window (verified
       // via probes/libgodot_window_lifecycle_test.cpp, db8a3d6) -- no
       // event-watcher registration needed, unlike SDL3's path.
   }

   inline void input_begin_frame() {
       Input* input = Input::get_singleton();
       if (!input) return;
       for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc) {
           Key k = _godot_input::scancode_table()[sc];
           if (k == Key::NONE) continue;
           bool down = input->is_physical_key_pressed(k);
           _godot_input::s_pressed_this_frame[sc] = down && !_godot_input::s_down_prev[sc];
           _godot_input::s_down_prev[sc] = down;
       }
       // b starts at 1, not 0 -- Godot's MouseButton::NONE==0 is not a
       // valid button and is_mouse_button_pressed(NONE) hits an internal
       // ERR_FAIL_COND in mouse_button_to_mask() (found live: first run
       // printed 60 recoverable "Condition button==NONE" errors before
       // this fix, no crash but real noise -- traced to the real Godot
       // source, not guessed).
       for (int b = 1; b < 6; ++b) {
           bool down = input->is_mouse_button_pressed((MouseButton)b);
           _godot_input::s_mouse_pressed_this_frame[b] = down && !_godot_input::s_mouse_down_prev[b];
           _godot_input::s_mouse_down_prev[b] = down;
       }
   }

   inline bool input_key_down(int key) {
       if (key < 0 || key >= SDL_SCANCODE_COUNT) return false;
       Key k = _godot_input::scancode_table()[key];
       if (k == Key::NONE) return false;
       Input* input = Input::get_singleton();
       return input && input->is_physical_key_pressed(k);
   }
   inline bool input_key_pressed(int key) {
       return (key >= 0 && key < SDL_SCANCODE_COUNT) ? _godot_input::s_pressed_this_frame[key] : false;
   }
   inline bool input_mouse_pressed(int btn) {
       return (btn >= 0 && btn < 6) ? _godot_input::s_mouse_pressed_this_frame[btn] : false;
   }
   inline bool input_mouse_down(int btn) {
       Input* input = Input::get_singleton();
       return input && input->is_mouse_button_pressed((MouseButton)btn);
   }
   inline float input_mouse_x() { Input* i = Input::get_singleton(); return i ? i->get_mouse_position().x : 0.f; }
   inline float input_mouse_y() { Input* i = Input::get_singleton(); return i ? i->get_mouse_position().y : 0.f; }
   // NOT YET IMPLEMENTED: Godot represents mouse wheel as discrete
   // WHEEL_UP/WHEEL_DOWN button-press events, not a continuous SDL-style
   // accumulator -- real capture needs a Node's _input() callback
   // (SceneTree hookup), unspiked this session. Returns 0 rather than
   // guessing at a wrong implementation (project rule: no speculative
   // fixes without hard verification).
   inline float input_get_scroll_y() { return 0.f; }
   inline bool  input_should_quit()   { return _godot_input::s_quit; }
   // Set by window.h's window_end_frame() from instance->iteration()'s
   // return value (true = Main::iteration() requested exit -- verified
   // via main.cpp source, Main::iteration()'s `exit` local).
   inline void _godot_input_set_quit(bool q) { _godot_input::s_quit = q; }

#else // !MD_USE_LIBGODOT -- existing SDL3 path, unchanged.

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
#  define KEY_M             SDL_SCANCODE_M
#  define KEY_R             SDL_SCANCODE_R
#  define KEY_S             SDL_SCANCODE_S
#  define KEY_T             SDL_SCANCODE_T
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
#  define KEY_ENTER         SDL_SCANCODE_RETURN
#  define KEY_SPACE         SDL_SCANCODE_SPACE
#  define KEY_BACKSPACE     SDL_SCANCODE_BACKSPACE
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
       // Continuous held-state, kept in sync by the watcher on both
       // SDL_EVENT_KEY_DOWN/UP — unlike SDL_GetKeyboardState(), this also
       // reflects synthetic keys injected via SDL_PushEvent (the Live
       // UI-driver's md.ui.hold_key), since SDL only updates its own
       // internal keyboard array from real platform-driver key events, not
       // from queued/pushed ones. See input_key_down's doc comment.
       inline bool  s_down [SDL_SCANCODE_COUNT] = {};
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
       if (ev->type == SDL_EVENT_KEY_DOWN) {
           int sc = (int)ev->key.scancode;
           if (sc >= 0 && sc < SDL_SCANCODE_COUNT) {
               if (!ev->key.repeat) _sdl3_input::s_next[sc] = true;
               _sdl3_input::s_down[sc] = true;
           }
       }
       if (ev->type == SDL_EVENT_KEY_UP) {
           int sc = (int)ev->key.scancode;
           if (sc >= 0 && sc < SDL_SCANCODE_COUNT)
               _sdl3_input::s_down[sc] = false;
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

   // Reads our own watcher-tracked held-state, NOT SDL_GetKeyboardState() —
   // that array is only updated by the platform video driver processing a
   // genuine hardware key event; events injected via SDL_PushEvent (Live
   // UI-driver's md.ui.hold_key) reach the queue/watcher fine but never
   // reach that internal array, so WASD movement silently did nothing when
   // driven synthetically even though the queued event itself was real.
   inline bool input_key_down(int key) {
       return (key >= 0 && key < SDL_SCANCODE_COUNT) ? _sdl3_input::s_down[key] : false;
   }
   inline bool input_key_pressed(int key) {
       return (key >= 0 && key < SDL_SCANCODE_COUNT)
              ? _sdl3_input::s_keys[key] : false;
   }
   inline bool  input_mouse_pressed(int btn) {
       return (btn > 0 && btn < 6) ? _sdl3_input::s_mouse[btn] : false;
   }
   inline bool  input_mouse_down(int btn) {
       return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(btn)) != 0;
   }
   inline float input_mouse_x()       { float x=0; SDL_GetMouseState(&x, nullptr); return x; }
   inline float input_mouse_y()       { float y=0; SDL_GetMouseState(nullptr, &y); return y; }
   inline float input_get_scroll_y()  { return _sdl3_input::s_scroll_y; }
   inline bool  input_should_quit()   { return _sdl3_input::s_quit; }

#endif // MD_USE_LIBGODOT
