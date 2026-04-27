#ifdef DEBUG
#pragma once
#include "raylib.h"
#ifdef USE_SDL3
#  include "backends/imgui_impl_sdl3.h"
#  include "backends/imgui_impl_opengl3.h"
#else
#  include "rlImGui.h"
#endif
#include "imgui.h"
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/ecs/engine_context.h>
#include <monkey_dust/world/spatial_grid.h>
#include <monkey_dust/nav/nav_system.h>
#include <entt/entt.hpp>

class DebugSystem {
public:
    static DebugSystem& Get() { static DebugSystem inst; return inst; }

    bool overlay_on  = false;
    bool grid_on     = false;
    bool navmesh_on  = false;
    bool clean_mode  = false;

    entt::entity selected_entity = entt::null;

    void HandleInput();
    void DrawOverlay(const md::EngineContext& ctx, Camera3D cam,
                     const SpatialGrid& grid);
    void Draw3DOverlay(Camera3D cam);
    void DrawHotReload(float dt);
    void DrawSpatialGridOverlay(Camera3D cam, const SpatialGrid& grid);
    void DrawNavMeshWireframe(Camera3D cam);

private:
    void DrawEntityInspector();
    void DrawEntityList();
    void DrawPerformanceOverlay(const md::EngineContext& ctx);

    char entity_filter_[64]  = {};
    int  entity_type_filter_ = 0;

    static constexpr int PERF_HISTORY = 120;
    float fps_history_  [PERF_HISTORY] = {};
    float logic_history_[PERF_HISTORY] = {};
    int   perf_idx_ = 0;

    long  last_buildings_mtime = 0;
    float reload_debounce      = 0.0f;
};
#endif // DEBUG
