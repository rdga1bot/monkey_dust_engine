#ifdef DEBUG
#include <monkey_dust/tools/debug_system.h>
#include <monkey_dust/tools/timing_system.h>
#include <cmath>
#include <cstdio>
#include <cstring>

void DebugSystem::DrawOverlay(const md::EngineContext& ctx, const MdCamera& /*cam*/,
                               const SpatialGrid& /*grid*/) {
    if (!overlay_on) return;
    DrawEntityInspector();
    DrawEntityList();
    DrawPerformanceOverlay(ctx);
}

void DebugSystem::Draw3DOverlay(const MdCamera& /*cam*/) {
    if (!overlay_on) return;
}

void DebugSystem::DrawHotReload(float dt) {
    reload_debounce -= dt;
    if (reload_debounce > 0.0f) {
        ImGui::Begin("Hot Reload");
        ImGui::Text("Reloaded!");
        ImGui::End();
    }
}

void DebugSystem::DrawSpatialGridOverlay(const MdCamera& /*cam*/,
                                          const SpatialGrid& /*grid*/) {
    if (!grid_on) return;
}

void DebugSystem::DrawNavMeshWireframe(const MdCamera& /*cam*/) {
    if (!navmesh_on) return;
}

void DebugSystem::DrawEntityInspector() {
    if (selected_entity == entt::null) return;
    ImGui::Begin("Entity Inspector");
    ImGui::Text("Entity ID: %u", (uint32_t)selected_entity);
    ImGui::End();
}

void DebugSystem::DrawEntityList() {
    ImGui::Begin("Entities");
    ImGui::InputText("Filter", entity_filter_, sizeof(entity_filter_));
    auto& reg = Registry::Get();
    int shown = 0;
    for (auto e : reg.storage<entt::entity>()) {
        if (shown >= 512) break;
        char label[32];
        snprintf(label, sizeof(label), "Entity %u", (uint32_t)e);
        if (entity_filter_[0] != '\0' &&
            strstr(label, entity_filter_) == nullptr) return;
        bool selected = (e == selected_entity);
        if (ImGui::Selectable(label, selected))
            selected_entity = e;
        ++shown;
    }
    ImGui::End();
}

void DebugSystem::DrawPerformanceOverlay(const md::EngineContext& ctx) {
    if (perf_idx_ < PERF_HISTORY) {
        fps_history_[perf_idx_] = ctx.fps;
    }
    perf_idx_ = (perf_idx_ + 1) % PERF_HISTORY;

    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.4f);
    ImGui::Begin("Perf", nullptr,
                 ImGuiWindowFlags_NoDecoration |
                 ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %.1f  dt: %.2fms  tick: %u",
                ctx.fps, ctx.delta_time * 1000.0f, ctx.logic_tick);
    ImGui::PlotLines("##fps", fps_history_, PERF_HISTORY, perf_idx_,
                     nullptr, 0.0f, 120.0f, {200, 40});
    ImGui::End();
}
#endif // DEBUG
