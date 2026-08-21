#pragma once
// Platform window + frame lifecycle abstraction.
// Rule M-A: include only from Main.cpp and EditorMain.cpp.
//
// MD_USE_LIBGODOT (Фаза C.5 Platform-шар, 2026-08-20): full-cutover
// backend, mutually exclusive with the SDL3 path below -- see
// docs/LIBGODOT_MIGRATION_PLAN_PHASE_A_F.md's "Platform-шар" section
// and engine/include/monkey_dust/platform/input.h's matching comment.
// Same public API (window_init/window_get_width/window_begin_frame/
// etc signatures) -- game/src call sites need ZERO changes.
#ifdef MD_USE_LIBGODOT

#  include "core/extension/libgodot.h"
#  include "core/extension/godot_instance.h"
#  include "core/os/os.h"
#  include "servers/display/display_server.h"
#  include <monkey_dust/render/md_camera.h>
#  include <monkey_dust/platform/input.h> // _godot_input_set_quit()
#  include <cstdio>
#  include <string>
#  include <vector>

#  define SetTraceLogLevel(...)  ((void)0)

   namespace _wnd_godot {
       inline GodotInstance*& instance() { static GodotInstance* i = nullptr; return i; }
       inline int& width()  { static int v = 1280; return v; }
       inline int& height() { static int v = 720;  return v; }
   }

   static GDExtensionBool _wnd_godot_minimal_init(GDExtensionInterfaceGetProcAddress,
                                                   GDExtensionClassLibraryPtr,
                                                   GDExtensionInitialization *r_init) {
       r_init->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
       r_init->userdata = nullptr;
       r_init->initialize = [](void *, GDExtensionInitializationLevel) {};
       r_init->deinitialize = [](void *, GDExtensionInitializationLevel) {};
       return true;
   }

   // window_init() eq. -- unlike SDL3's explicit SDL_CreateWindow(), the
   // window appears IMPLICITLY inside libgodot_create_godot_instance()+
   // start() (--resolution argv). Verified live:
   // probes/libgodot_window_lifecycle_test.cpp (db8a3d6) -- create+start
   // 947.8ms, window_get_size() == exact requested resolution, 300-
   // iteration frame-loop stable (~2ms/iter, no hang).
   inline void window_init(int w, int h, const char* title) {
       int init_w = (w > 0) ? w : 1280;
       int init_h = (h > 0) ? h : 720;
       static std::string s_res = std::to_string(init_w) + "x" + std::to_string(init_h);
       static std::vector<std::string> arg_strings = {
           title ? title : "monkey_dust", "--rendering-method", "forward_plus", "--resolution", s_res
       };
       static std::vector<char*> arg_storage;
       arg_storage.clear();
       for (auto &s : arg_strings) arg_storage.push_back(&s[0]);
       arg_storage.push_back(nullptr);

       GDExtensionObjectPtr obj = libgodot_create_godot_instance(
           (int)arg_strings.size(), arg_storage.data(), _wnd_godot_minimal_init);
       if (!obj) { fprintf(stderr, "[window] libgodot_create_godot_instance failed\n"); return; }
       GodotInstance* inst = (GodotInstance*)obj;
       if (!inst->start()) { fprintf(stderr, "[window] GodotInstance::start() failed\n"); return; }
       _wnd_godot::instance() = inst;
       _wnd_godot::width()  = init_w;
       _wnd_godot::height() = init_h;
   }

   inline void window_shutdown() {
       if (_wnd_godot::instance()) {
           libgodot_destroy_godot_instance(_wnd_godot::instance());
           _wnd_godot::instance() = nullptr;
       }
   }

   // NOT YET IMPLEMENTED: real vsync-mode switching unspiked this
   // session (Godot controls this via --disable-vsync argv at instance
   // creation, not a documented runtime toggle) -- no-op rather than a
   // guessed API call.
   inline void window_set_vsync(int /*fps*/) {}

   // window_get_width/height() eq. -- always re-reads DisplayServer
   // (live, not the SDL3 path's cached _wnd::width()/height()) since
   // window_begin_frame() below already keeps _wnd_godot::width/height
   // in sync every frame, matching the SDL3 path's own resize-tracking
   // behavior (window.h's SDL_GetWindowSize() call in window_begin_frame()).
   inline int  window_get_width()  { return _wnd_godot::width(); }
   inline int  window_get_height() { return _wnd_godot::height(); }
   inline float window_get_time_s() {
       return _wnd_godot::instance() ? (float)(OS::get_singleton()->get_ticks_msec() * 0.001) : 0.f;
   }

   inline void window_set_size(int w, int h) {
       if (w <= 0 || h <= 0) return;
       DisplayServer* ds = DisplayServer::get_singleton();
       if (!ds) return;
       ds->window_set_size(Size2i(w, h));
   }

   inline void window_begin_frame() {
       DisplayServer* ds = DisplayServer::get_singleton();
       if (!ds) return;
       Size2i sz = ds->window_get_size();
       _wnd_godot::width()  = sz.x;
       _wnd_godot::height() = sz.y;
   }

   // No-ops under LibGodot: RenderingServer handles depth-state
   // internally per-material, no explicit glEnable(GL_DEPTH_TEST)
   // equivalent needed at the call site (unlike the OpenGL path below).
   inline void window_begin_3d(const MdCamera& /*cam*/) {}
   inline void window_end_3d() {}

   // window_end_frame() eq. -- instance->iteration() both renders AND
   // pumps OS/input events (verified: probes/libgodot_window_lifecycle_
   // test.cpp's 300-iteration loop, probes/libgodot_input_map_test.cpp's
   // Input singleton state). Return value == true means Main::iteration()
   // requested exit (verified from main/main.cpp source, the `exit` local
   // var) -- forwarded to input.h's quit flag via _godot_input_set_quit().
   inline void window_end_frame() {
       if (!_wnd_godot::instance()) return;
       bool want_quit = _wnd_godot::instance()->iteration();
       if (want_quit) _godot_input_set_quit(true);
   }

#else // !MD_USE_LIBGODOT -- existing SDL3+OpenGL/SDL_GPU path, unchanged.

// Pure SDL3 + OpenGL context, no Raylib (M12.3+; the Raylib fallback path
// was removed 2026-08-09 -- USE_SDL3=ON has been the only buildable
// configuration since engine/CMakeLists.txt's own FATAL_ERROR gate, and
// raylib itself is headers-only in the tree now, not linked).
//   gladLoadGL loads GL function pointers from SDL_GL_GetProcAddress.
//   DrawText/DrawRectangle replaced by MdDraw2D (M12.10).
//
// imgui_init / imgui_new_frame / imgui_render / imgui_shutdown:
//   Available only under #ifdef DEBUG.

#  include <SDL3/SDL.h>
#  include "glad.h"
#  include <monkey_dust/render/md_camera.h>
#  include <cstdio>
#ifdef MD_SDL_GPU
#  include <monkey_dust/render/gpu_device.h>
#endif

   // Compat no-ops for Raylib calls that may still appear in non-migrated game code.
#  define SetTraceLogLevel(...)  ((void)0)

   namespace _wnd {
       inline SDL_Window*&   ptr()  { static SDL_Window*   w = nullptr; return w; }
       inline SDL_GLContext&  ctx()  { static SDL_GLContext  c = nullptr; return c; }
       inline int&           width()  { static int v = 1280; return v; }
       inline int&           height() { static int v = 720;  return v; }
   }

   // w=0/h=0 → auto-detect from the display this window opens on.
   inline void window_init(int w, int h, const char* title) {
       SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

       int init_w = (w > 0) ? w : 1280;
       int init_h = (h > 0) ? h : 720;

#ifdef MD_SDL_GPU
       // Pure SDL_GPU (Vulkan/Metal): no OpenGL context.
       // SDL_ClaimWindowForGPUDevice requires the window NOT have an active GL context.
       _wnd::ptr() = SDL_CreateWindow(title, init_w, init_h,
           SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
#else
       SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
       SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
       SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
       SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
       SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
       _wnd::ptr() = SDL_CreateWindow(title, init_w, init_h,
           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
#endif
       if (!_wnd::ptr()) {
           fprintf(stderr, "[window] SDL_CreateWindow: %s\n", SDL_GetError());
           return;
       }

       // Auto-detect: resize to usable display area.
       if (w <= 0 || h <= 0) {
           SDL_DisplayID disp = SDL_GetDisplayForWindow(_wnd::ptr());
           SDL_Rect bounds = {};
           if (SDL_GetDisplayUsableBounds(disp, &bounds) &&
               bounds.w > 0 && bounds.h > 0) {
               init_w = bounds.w;
               init_h = bounds.h;
               SDL_SetWindowSize(_wnd::ptr(), init_w, init_h);
           }
       }

       _wnd::width()  = init_w;
       _wnd::height() = init_h;

#ifndef MD_SDL_GPU
       _wnd::ctx() = SDL_GL_CreateContext(_wnd::ptr());
       if (!_wnd::ctx()) {
           fprintf(stderr, "[window] SDL_GL_CreateContext: %s\n", SDL_GetError());
           return;
       }
       SDL_GL_MakeCurrent(_wnd::ptr(), _wnd::ctx());
       SDL_GL_SetSwapInterval(1);
       gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
#endif
   }

   inline void window_shutdown() {
#ifndef MD_SDL_GPU
       SDL_GL_DestroyContext(_wnd::ctx()); _wnd::ctx() = nullptr;
#endif
       SDL_DestroyWindow(_wnd::ptr());     _wnd::ptr() = nullptr;
       SDL_Quit();
   }

   inline void window_set_vsync(int fps) {
#ifdef MD_SDL_GPU
       auto& dev = md::GpuDevice::Get();
       if (dev.IsReady()) {
           bool has_vsync   = SDL_WindowSupportsGPUPresentMode(dev.SDLDevice(), dev.Window(), SDL_GPU_PRESENTMODE_VSYNC);
           bool has_mailbox = SDL_WindowSupportsGPUPresentMode(dev.SDLDevice(), dev.Window(), SDL_GPU_PRESENTMODE_MAILBOX);
           SDL_Log("[vsync] supported: vsync=%d mailbox=%d requested=%s",
                   has_vsync, has_mailbox, fps > 0 ? "vsync" : "no-vsync");
           SDL_GPUPresentMode mode;
           if (fps > 0 && has_vsync) {
               mode = SDL_GPU_PRESENTMODE_VSYNC;
           } else if (!fps && has_mailbox) {
               mode = SDL_GPU_PRESENTMODE_MAILBOX;
           } else {
               mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
           }
           bool ok = SDL_SetGPUSwapchainParameters(dev.SDLDevice(), dev.Window(),
               SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
           SDL_Log("[vsync] SetGPUSwapchainParameters mode=%d ok=%d err=%s",
                   (int)mode, ok, ok ? "none" : SDL_GetError());
       }
#else
       SDL_GL_SetSwapInterval(fps > 0 ? 1 : 0);
#endif
   }
   inline int  window_get_width()  { return _wnd::width(); }
   inline int  window_get_height() { return _wnd::height(); }
   inline float window_get_time_s() { return (float)(SDL_GetTicks() * 0.001); }

   // terrain-perf-measure (2026-08-12): live resize for the resolution-
   // scaling frame-time test (md.set_window_size). window_begin_frame()
   // re-reads the real size from SDL every frame (SDL_GetWindowSize), and
   // TerrainShadingProjected::EnsureSize already tracks window resizes
   // correctly (diagnosed/fixed 2026-08-02) -- so no extra plumbing needed
   // beyond the resize call itself, EXCEPT: window_init() creates the
   // window with SDL_WINDOW_MAXIMIZED (line ~44), and SDL_SetWindowSize()
   // is a documented no-op on a maximized window on most platforms/WMs --
   // confirmed empirically (2026-08-12): every resize call in a 4-point
   // resolution-scaling test silently did nothing, gbuf_w/gbuf_h stayed
   // at the maximized 1920x1056 the whole time, and the resulting "flat
   // GPU cost vs resolution" data was measuring one single resolution
   // four times, not four different ones. SDL_RestoreWindow() un-maximizes
   // first so the resize actually takes effect.
   inline void window_set_size(int w, int h) {
       if (w <= 0 || h <= 0) return;
       SDL_RestoreWindow(_wnd::ptr());
       SDL_SetWindowSize(_wnd::ptr(), w, h);
   }

   // Sync window size. SDL_GPU: frame clear handled by render pass; no GL calls.
   inline void window_begin_frame() {
       int w = 0, h = 0;
       SDL_GetWindowSize(_wnd::ptr(), &w, &h);
       if (w != _wnd::width() || h != _wnd::height()) {
           _wnd::width()  = w;
           _wnd::height() = h;
       }
#ifndef MD_SDL_GPU
       glViewport(0, 0, w, h);
       glClearColor(46.f/255.f, 51.f/255.f, 64.f/255.f, 1.f);
       glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif
   }

   inline void window_begin_3d(const MdCamera& /*cam*/) {
#ifndef MD_SDL_GPU
       glEnable(GL_DEPTH_TEST);
#endif
   }
   inline void window_end_3d() {
#ifndef MD_SDL_GPU
       glDisable(GL_DEPTH_TEST);
#endif
   }

   // SDL_GPU: frame presented via GpuDevice::Submit(); just pump events.
   inline void window_end_frame() {
#ifndef MD_SDL_GPU
       SDL_GL_SwapWindow(_wnd::ptr());
#endif
       // Events are NOT drained here — they stay in the SDL queue until the next
       // frame's pump (imgui_pump_events or the caller's own SDL_PollEvent loop).
       // Draining here would silently consume events between imgui_new_frame and
       // window_end_frame, causing missed clicks in ImGui.
   }

#endif // MD_USE_LIBGODOT
