#pragma once
// Platform window + frame lifecycle abstraction.
// Rule M-A: include only from Main.cpp and EditorMain.cpp.
//
// USE_SDL3 path: Raylib (PLATFORM=SDL) initialises SDL3 window + rlgl via InitWindow.
//   We then bypass BeginDrawing/EndDrawing, take over frame management directly.
//   gladLoadGL is called once to load our own glad instance for direct GL calls.
//
// !USE_SDL3 path: thin Raylib wrappers — behaviour identical to before M6.

#ifdef USE_SDL3
#  include "raylib.h"        // InitWindow (PLATFORM=SDL → SDL3 + rlgl), CloseWindow
#  include "rlgl.h"          // rlDrawRenderBatchActive
#  include <SDL3/SDL.h>
#  include "external/glad.h"
#  include <cstdio>

   namespace _wnd {
       inline SDL_Window*& ptr()    { static SDL_Window* w = nullptr; return w; }
       inline int&         width()  { static int v = 1280; return v; }
       inline int&         height() { static int v = 720;  return v; }
   }

   // w=0/h=0 → auto-detect from the display this window opens on.
   inline void window_init(int w, int h, const char* title) {
       // Use a temporary size for InitWindow; auto-detect replaces it below.
       int init_w = (w > 0) ? w : 1280;
       int init_h = (h > 0) ? h : 720;
       InitWindow(init_w, init_h, title);
       _wnd::ptr() = (SDL_Window*)GetWindowHandle();

       // Make resizable before any size changes.
       SDL_SetWindowResizable(_wnd::ptr(), true);

       // Auto-detect: size the window to the display it opened on.
       if (w <= 0 || h <= 0) {
           SDL_DisplayID disp = SDL_GetDisplayForWindow(_wnd::ptr());
           SDL_Rect bounds = {};
           if (SDL_GetDisplayUsableBounds(disp, &bounds) == 0 &&
               bounds.w > 0 && bounds.h > 0) {
               init_w = bounds.w;
               init_h = bounds.h;
           }
           // SetWindowSize tells both SDL3 and Raylib's internal state.
           SetWindowSize(init_w, init_h);
       }

       _wnd::width()  = init_w;
       _wnd::height() = init_h;
       gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
   }
   inline void window_shutdown()             { CloseWindow(); }
   inline void window_set_vsync(int /*fps*/) { SDL_GL_SetSwapInterval(1); }
   inline int  window_get_width()            { return _wnd::width(); }
   inline int  window_get_height()           { return _wnd::height(); }

   // Replaces BeginDrawing(): sync window size, clear buffers, set up Raylib 2D
   // orthographic matrix so DrawText/DrawRectangle work correctly outside BeginMode3D.
   inline void window_begin_frame() {
       int w, h;
       SDL_GetWindowSize(_wnd::ptr(), &w, &h);
       if (w != _wnd::width() || h != _wnd::height()) {
           _wnd::width()  = w;
           _wnd::height() = h;
           SetWindowSize(w, h);  // sync Raylib CORE.Window so GetScreenWidth/Height is correct
       }
       glViewport(0, 0, w, h);
       glClearColor(46.f/255.f, 51.f/255.f, 64.f/255.f, 1.f);
       glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
       // BeginDrawing() is not called under USE_SDL3, but Raylib's 2D batch pipeline
       // (DrawText, DrawRectangle) needs an orthographic projection to be current.
       // BeginMode3D will push this matrix and restore it in EndMode3D.
       rlMatrixMode(RL_PROJECTION);
       rlLoadIdentity();
       rlOrtho(0, w, h, 0, 0.0, 1.0);
       rlMatrixMode(RL_MODELVIEW);
       rlLoadIdentity();
   }

   // Replaces EndDrawing(): flush Raylib batch, swap, pump SDL events.
   // event_cb (optional) — called per-event before EventWatcher; used by Main.cpp to
   // forward events to ImGui_ImplSDL3_ProcessEvent in DEBUG builds.
   inline void window_end_frame(void(*event_cb)(const SDL_Event*) = nullptr) {
       rlDrawRenderBatchActive();
       SDL_GL_SwapWindow(_wnd::ptr());
       SDL_Event e;
       while (SDL_PollEvent(&e)) {
           if (event_cb) event_cb(&e);
       }
   }

#else  // ── Raylib path (default, !USE_SDL3) ─────────────────────────────────
#  include "raylib.h"

   // w=0/h=0 → auto-detect from current monitor.
   inline void window_init(int w, int h, const char* title) {
       int init_w = (w > 0) ? w : 1280;
       int init_h = (h > 0) ? h : 720;
       InitWindow(init_w, init_h, title);
       SetWindowState(FLAG_WINDOW_RESIZABLE);
       if (w <= 0 || h <= 0) {
           int mon = GetCurrentMonitor();
           int mw  = GetMonitorWidth(mon);
           int mh  = GetMonitorHeight(mon);
           if (mw > 0 && mh > 0) SetWindowSize(mw, mh);
       }
   }
   inline void window_shutdown()            { CloseWindow(); }
   inline void window_set_vsync(int fps)    { SetTargetFPS(fps); }
   inline int  window_get_width()           { return GetScreenWidth(); }
   inline int  window_get_height()          { return GetScreenHeight(); }
   inline void window_begin_frame()         { BeginDrawing(); }
   inline void window_end_frame()           { EndDrawing(); }

#endif // USE_SDL3
