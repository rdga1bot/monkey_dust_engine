#pragma once
// LibGodot migration Фаза E.1 — editor GUI backend adapter.
// Компілюється лише коли MD_USE_LIBGODOT визначено (той самий гейт, що
// libgodot_bridge.h) — engine/ і далі компілюється без libgodot для
// звичайного USE_SDL3-шляху (dual-run, Фаза A CONSTRAINT).
//
// CONSTRAINT (docs/LIBGODOT_MIGRATION_PLAN_PHASE_A_F.md, Фаза E.1):
//   - Усі 68 editor_*.h/.cpp панелей ЛИШАЮТЬСЯ БЕЗ ЗМІН (DUAL, НЕ
//     FULL_MIGRATE) — вони викликають лише ImGui::* API, не знають про
//     backend взагалі.
//   - Adapter лише перенаправляє ImGui draw-команди на RenderingServer,
//     той самий контракт, що game/src/imgui_platform.h's SDL_GPU шлях
//     (imgui_init/imgui_new_frame/imgui_render/imgui_shutdown), НЕ
//     called напряму панелями.
//
// РОЗВІДКА Фази E.0 (probes/libgodot_canvas_item_test.cpp, живо
// підтверджено): RenderingServer::canvas_item_add_triangle_array()
// -- ТОЧНО ImGui's ImDrawCmd форма (indexed triangle list + одна
// текстура на виклик). Ця реалізація -- РЕАЛЬНИЙ адаптер, не
// синтетичний доказ: справжній ImGui::Render()'s ImDrawData цикл.
//
// Dear ImGui 1.92 texture API (не стара одна-TexID модель):
// ImGuiBackendFlags_RendererHasTextures + ImTextureData::Status-машина
// (WantCreate/WantUpdates/WantDestroy) -- сама бібліотека керує font
// atlas lifecycle, backend лише реагує на статус через draw_data->
// Textures[]/GetPlatformIO().Textures[].
#ifdef MD_USE_LIBGODOT

#include <cstdint>

struct ImDrawData;

// viewport_id -- вже створений viewport (RID::get_id()), той самий
// патерн, що LibGodotBridge's uint64_t-опаковий публічний API.
// Створює власний canvas, attach'ає до viewport-а.
bool ImGui_ImplRenderingServer_Init(uint64_t viewport_id);
// free_rid() усіх canvas_item-ів пулу + canvas. Font/user текстури,
// зареєстровані через ImTextureData -- звільняються окремо, у
// RenderDrawData()'s WantDestroy обробці (ImGui сам керує їх
// життєвим циклом, не canvas item pool-ом).
void ImGui_ImplRenderingServer_Shutdown();
// No-op зараз (display size не залежить від renderer backend-у в
// 1.92's моделі) -- викликається для симетрії з іншими imgui_impl_*
// backend-ами (той самий контракт, що ImGui_ImplSDLGPU3_NewFrame()).
void ImGui_ImplRenderingServer_NewFrame();
// ImGui::Render()'s ImDrawData -> canvas_item_clear()+
// canvas_item_add_triangle_array() виклики, один canvas_item на
// ImDrawCmd (пул, що росте, ніколи не звужується між кадрами --
// уникає per-frame RID create/free churn).
void ImGui_ImplRenderingServer_RenderDrawData(ImDrawData* draw_data);

#endif // MD_USE_LIBGODOT
