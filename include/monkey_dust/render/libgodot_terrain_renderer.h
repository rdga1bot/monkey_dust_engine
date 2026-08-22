#pragma once
// LibGodot migration Крок 1e (task #534) — shared terrain renderer
// module. Компілюється лише коли MD_USE_LIBGODOT визначено (той самий
// гейт, що libgodot_bridge.h/imgui_impl_renderingserver.h) — engine/ і
// далі компілюється без libgodot для звичайного USE_SDL3-шляху.
//
// Витягнуто з game/src/main_libgodot.cpp (Крок 1a-1d, tasks #530-533),
// нуль поведінкових змін — той самий real Kenshi heightmap + multi-tile
// LOD streaming + skirts/geomorph + cliff-blending + fog шлях, тепер
// callable з БУДЬ-ЯКОГО MD_USE_LIBGODOT таргету (гра вже, майбутній
// monkey_dust_libgodot_editor's F3 termeйн-інструменти — Крок 3d).
//
// Публічний API навмисно оперує uint64_t (scenario RID's get_id()), не
// реальним godot::RID типом — той самий опаковий-хендл принцип, що
// libgodot_bridge.h/imgui_impl_renderingserver.h вже встановили.
// RenderingServer саме по собі береться через RenderingServer::
// get_singleton() всередині .cpp, не передається викликачем.
#ifdef MD_USE_LIBGODOT

#include <string>

// Одноразова ініціалізація: завантажує реальний Kenshi heightmap
// (world_hmap.r16 via TerrainAtlas), будує спільний unit-quad mesh +
// shader, вантажить tex_colour/tex_ground_baked + (за наявності)
// per-biome cliff-текстур-масив. launch_cwd — той самий шлях, що
// window_init()'s Godot-project chdir() ламає для відносних шляхів
// (main_libgodot.cpp вже передає його ж усюди).
void LibgodotTerrain_Init(const std::string& launch_cwd);

// Щокадровий стрімінг-виклик (Крок 1a/1b): TerrainQuadtree::
// SelectVisible() навколо камери, створює/перевикористовує per-node RS
// instance+material (кешовано по (depth,gx,gz)), evict'ить вузли поза
// видимістю. scenario_id — RID::get_id() активного сценарію.
void LibgodotTerrain_Update(uint64_t scenario_id,
                            float cam_x, float cam_y, float cam_z,
                            float cam_target_x, float cam_target_y, float cam_target_z);

// MUST run BEFORE LibGodotBridge::Shutdown() (той самий Ref<>-outliving-
// RS-usage bug class, що npc_skin_ref/skin/doc/state їхні власні
// коментарі документують) — вивільняє усі закешовані node instance RID
// через RenderingServer::get_singleton(), потім скидає решту статиків.
void LibgodotTerrain_Shutdown();

#endif // MD_USE_LIBGODOT
