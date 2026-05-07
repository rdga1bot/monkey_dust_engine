#pragma once
// Platform window + frame lifecycle abstraction.
// Rule M-A: include only from Main.cpp and EditorMain.cpp.
//
// USE_SDL3 path (M12.3+): pure SDL3 + OpenGL context, no Raylib.
//   gladLoadGL loads GL function pointers from SDL_GL_GetProcAddress.
//   DrawText/DrawRectangle replaced by MdDraw2D (M12.10).
//
// !USE_SDL3 path: thin Raylib wrappers.
//
// imgui_init / imgui_new_frame / imgui_render / imgui_shutdown:
//   Available only under #ifdef DEBUG.

#ifdef USE_SDL3
#  include <SDL3/SDL.h>
#  include "glad.h"
#  include <monkey_dust/render/md_camera.h>
#  include <cstdio>
#if defined(DEBUG) || defined(MONKEY_DUST_STANDALONE_EDITOR)
#  include "backends/imgui_impl_sdl3.h"
#  include "backends/imgui_impl_opengl3.h"
#  include "imgui.h"
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

   inline void window_set_vsync(int /*fps*/) {
#ifndef MD_SDL_GPU
       SDL_GL_SetSwapInterval(1);
#endif
   }
   inline int  window_get_width()  { return _wnd::width(); }
   inline int  window_get_height() { return _wnd::height(); }
   inline float window_get_time_s() { return (float)(SDL_GetTicks() * 0.001); }

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
       SDL_Event e;
       while (SDL_PollEvent(&e)) {
#if (defined(DEBUG) || defined(MONKEY_DUST_STANDALONE_EDITOR))
           ImGui_ImplSDL3_ProcessEvent(&e);
#endif
#ifdef MD_SDL_GPU
           // Re-maximize when moved to a different display.
           if (e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED)
               SDL_MaximizeWindow(_wnd::ptr());
#endif
       }
   }

// ── ImGui functions ──────────────────────────────────────────────────────────
// Non-SDL_GPU path: imgui_impl_opengl3 + imgui_impl_sdl3
#if (defined(DEBUG) || defined(MONKEY_DUST_STANDALONE_EDITOR)) && !defined(MD_SDL_GPU)
   inline void imgui_init() {
       ImGui::CreateContext();
       ImGui_ImplSDL3_InitForOpenGL(_wnd::ptr(), _wnd::ctx());
       ImGui_ImplOpenGL3_Init("#version 430");
   }
   inline void imgui_shutdown() {
       ImGui_ImplOpenGL3_Shutdown();
       ImGui_ImplSDL3_Shutdown();
       ImGui::DestroyContext();
   }
   inline void imgui_new_frame() {
       ImGui_ImplOpenGL3_NewFrame();
       ImGui_ImplSDL3_NewFrame();
       ImGui::NewFrame();
   }
   inline void imgui_render() {
       ImGui::Render();
       ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
   }
#endif

// SDL_GPU path: imgui_impl_sdlgpu3 + imgui_impl_sdl3
// imgui_sdlgpu_init() must be called after GpuDevice::Init(), not at window creation.
// GPU upload + render pass done in main.cpp (needs GpuDevice) via imgui_sdlgpu_draw().
#if defined(MD_SDL_GPU) && defined(DEBUG)
#  include "backends/imgui_impl_sdl3.h"
#  include "backends/imgui_impl_sdlgpu3.h"
#  include "imgui.h"

   inline void imgui_init() { /* no-op: call imgui_sdlgpu_init() after GpuDevice is ready */ }
   inline void imgui_shutdown() {
       ImGui_ImplSDLGPU3_Shutdown();
       ImGui_ImplSDL3_Shutdown();
       ImGui::DestroyContext();
   }
   inline void imgui_sdlgpu_init(SDL_GPUDevice* device, SDL_GPUTextureFormat fmt) {
       ImGui::CreateContext();
       ImGui_ImplSDL3_InitForSDLGPU(_wnd::ptr());
       ImGui_ImplSDLGPU3_InitInfo info = {};
       info.Device            = device;
       info.ColorTargetFormat = fmt;
       ImGui_ImplSDLGPU3_Init(&info);
   }
   inline void imgui_new_frame() {
       ImGui_ImplSDLGPU3_NewFrame();
       ImGui_ImplSDL3_NewFrame();
       ImGui::NewFrame();
   }
   // Finalizes ImGui CPU draw lists — GPU upload/present done via imgui_sdlgpu_draw().
   inline void imgui_render() { ImGui::Render(); }
   // Call after all render passes but before Submit().
   // sc_tex = swapchain texture acquired via GpuDevice::AcquireSwapchainTexture().
   inline void imgui_sdlgpu_draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* sc_tex) {
       if (!sc_tex) return;
       ImDrawData* dd = ImGui::GetDrawData();
       if (!dd || dd->CmdListsCount == 0) return;
       ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
       SDL_GPUColorTargetInfo ct = {};
       ct.texture  = sc_tex;
       ct.load_op  = SDL_GPU_LOADOP_LOAD;
       ct.store_op = SDL_GPU_STOREOP_STORE;
       ct.cycle    = false;
       SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
       if (rp) {
           ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp);
           SDL_EndGPURenderPass(rp);
       }
   }
#endif

#else  // ── Raylib path (!USE_SDL3) ──────────────────────────────────────────
#  include "raylib.h"
#  include <monkey_dust/render/md_camera.h>
#ifdef DEBUG
#  include "rlImGui.h"
#  include "imgui.h"
#endif

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
       SetExitKey(KEY_NULL);
   }
   inline void window_shutdown()              { CloseWindow(); }
   inline void window_set_vsync(int fps)      { SetTargetFPS(fps); }
   inline int  window_get_width()             { return GetScreenWidth(); }
   inline int  window_get_height()            { return GetScreenHeight(); }
   inline float window_get_time_s()           { return (float)GetTime(); }
   inline void window_begin_frame()           { BeginDrawing(); ClearBackground({46, 51, 64, 255}); }
   inline void window_end_frame()             { EndDrawing(); }
   inline void window_begin_3d(const MdCamera& cam) { BeginMode3D(cam.ToRaylib()); }
   inline void window_end_3d()                { EndMode3D(); }

#ifdef DEBUG
   inline void imgui_init()      { rlImGuiSetup(true); }
   inline void imgui_shutdown()  { rlImGuiShutdown(); }
   inline void imgui_new_frame() { rlImGuiBegin(); }
   inline void imgui_render()    { rlImGuiEnd(); }
#endif

#endif // USE_SDL3
