#pragma once
// LibGodot migration — character preview module (parity gate blocker #5,
// docs/LIBGODOT_MIGRATION_PARITY_CHECKLIST.md). Компілюється лише коли
// MD_USE_LIBGODOT визначено (той самий гейт, що libgodot_bridge.h/
// libgodot_terrain_renderer.h) — engine/ і далі компілюється без libgodot
// для звичайного USE_SDL3-шляху (Фаза A CONSTRAINT).
//
// v1 REAL, SCOPED slice of tools/editor/editor_char_preview_sdlgpu.h's
// SDL_GPU feature (see that header's own doc comment + tools/editor/
// character_editor.h's 3-panel layout comment for the full reference
// scope this ports FROM): a real md_human.glb rendered in its OWN
// scenario/camera/viewport (offscreen, read back via viewport_get_
// texture() — same pattern probes/libgodot_integration_loop_test.cpp's
// screenshot path and imgui_impl_renderingserver.cpp's canvas_item
// texture handling both already rely on: a raw texture RID's get_id()
// IS the ImTextureID this backend expects, no separate registration
// step needed for a texture we created ourselves via the RS directly).
//
// Deliberately NOT ported in v1 (see docs/LIBGODOT_MIGRATION_PARITY_
// CHECKLIST.md's Character preview row for the honest scope note):
// body/face morph sliders (CharCustomization_ComputeScales/ApplyMorphs
// already exist engine-side and are reusable in a follow-up), hair
// style swap, clothing GLB layering. This v1 proves the real hard part
// — an RS-backed offscreen 3D preview viewport displayed live via
// ImGui::Image(), with orbit-camera input and ONE real customization
// control (skin tint) actually changing the rendered pixels.
//
// v2 (follow-up, same session): real clothing (.clothbin) + hair (.glb)
// attach/swap added below — see each function's own doc comment. Body/
// face morph sliders remain OUT of scope: the morph target data that
// does exist (game/data/props/md_human_morphs.glb, tools/morph_gen_b.py)
// is mathematically-synthesized and merged into md_human_t.glb (a
// DIFFERENT mesh than the md_human.glb this preview loads, 15504 verts,
// see tools/md_mesh_conv/merge_body_morphs.py) — not real morph data on
// the mesh this file actually renders, so per this migration's
// real-data-only discipline it is NOT wired here.
//
// Публічний API навмисно оперує uint64_t (texture RID's get_id()), не
// реальним godot::RID типом — той самий опаковий-хендл принцип, що
// libgodot_bridge.h/libgodot_terrain_renderer.h вже встановили.
#ifdef MD_USE_LIBGODOT

#include <string>
#include <cstdint>

// Одноразова ініціалізація: завантажує game/data/props/md_human.glb (той
// самий T-pose bind-pose mesh, що game/src/main_libgodot.cpp's real NPC-
// loading code already proves works end-to-end via GLTFDocument::
// append_from_file + generate_scene() + FindFirstMeshInstance — reused,
// not reinvented), реєструє unshaded+tint mesh instance у власному
// scenario, будує власний offscreen viewport+camera. launch_cwd — той
// самий шлях, що window_init()'s Godot-project chdir() ламає для
// відносних шляхів (main_libgodot.cpp вже передає його ж усюди).
// Повертає false (і лишає CharPreviewLibgodot_IsLoaded()==false) якщо
// .glb не знайдено/не має валідного MeshInstance3D — викликач сам
// вирішує, чи показувати панель у цьому разі.
bool CharPreviewLibgodot_Init(const std::string& launch_cwd);

// Щокадровий виклик: оновлює orbit-камеру (RMB-drag коли dragging=true,
// інакше повільний Kenshi-style idle auto-rotate — той самий "portrait
// auto-rotation коли не тягнуть" підхід, що tools/editor/editor_char_
// preview_runtime.cpp's s_portrait_mode коментар документує, спрощена
// неперервна версія замість оригінального ±45° осциляційного вікна) і
// перераховує camera_set_transform(). drag_dx/drag_dy — пікселі миші за
// кадр (тільки коли dragging=true); zoom_delta — довільна одиниця
// зближення/віддалення (додатне = ближче), той самий підхід, що W/S
// zoom у tools/editor/main_libgodot.cpp's OrbitCamera вже використовує
// для цього ж бекенду (scroll недоступний тут, input.h:187).
void CharPreviewLibgodot_Update(float dt, bool dragging, float drag_dx, float drag_dy, float zoom_delta);

// ОДИН реальний, наскрізь working customization control (skin tone) —
// оновлює preview-матеріалу vec4 "tint" uniform (той самий shader
// параметр, що game/src/main_libgodot.cpp's kUnshadedShaderSrc вже
// живо підтвердив: ALBEDO = tint.rgb). r/g/b у [0,1].
void CharPreviewLibgodot_SetSkinColor(float r, float g, float b);

// ── Clothing (parity gate blocker #5 follow-up) ─────────────────────────
// 3 slots, mirrors tools/editor/editor_char_preview_sdlgpu.h's
// CLOTH_MAX_SLOTS convention (slot 0=top, 1=bottom, 2=reserved — the real
// SDL3 item table, tools/editor/editor_char_preview_sdlgpu_internal.h's
// s_cloth_items[], only populates slots 0/1 today; this mirrors that, not
// a new convention). Real .clothbin binary (magic 'COLT' + nv + ni +
// flags + nv*SkinVertex(52B) + ni*u16-or-u32 indices — same format
// tools/editor/editor_char_preview_assets.cpp's LoadClothingSlot() reads,
// SkinVertex layout from engine/include/monkey_dust/render/skin_mesh.h).
// Only position+normal are used: this preview has no skeleton/animation,
// so a SkinVertex's position/normal fields (which are the mesh's
// bind-pose values) are exactly the static geometry we want — joint/
// weight columns exist in the file but are read past, not applied.
static constexpr int kCharPreviewClothSlots = 3;

// clothbin_path=nullptr clears the slot (same "None" convention as the
// SDL3 reference's LoadClothingSlot). r/g/b tints the slot's material
// (same unshaded tint shader as the body/skin control above) — applied
// even when clearing, so the next equip in that slot shows the right
// colour immediately. Returns false on a real load failure; also returns
// true for a successful clear.
bool CharPreviewLibgodot_SetClothingSlot(int slot, const char* clothbin_path,
                                          float r, float g, float b);

// Tint-only update (no reload) — for a ColorEdit3 dragged every frame,
// re-running CharPreviewLibgodot_SetClothingSlot's full clothbin parse +
// mesh rebuild per-frame would be wasteful and would tear down/rebuild
// the RS instance while dragging. No-op (returns false) if the slot has
// nothing equipped or the preview isn't initialized.
bool CharPreviewLibgodot_SetClothingTint(int slot, float r, float g, float b);

// ── Hair (parity gate blocker #5 follow-up) ─────────────────────────────
// Real game/data/hair/*.glb (30 real styles — see tools/editor/editor_
// char_preview_assets.cpp's s_hair_styles[] table, all confirmed present
// on disk) loaded via the SAME GLTFDocument::append_from_file +
// generate_scene + FindFirstMeshInstance path CharPreviewLibgodot_Init()
// already uses for the body mesh above — reused, not reinvented.
// hair_glb_path=nullptr clears (no hair shown).
bool CharPreviewLibgodot_SetHairStyle(const char* hair_glb_path,
                                       float r, float g, float b);

// Tint-only update (no reload) — same rationale as CharPreviewLibgodot_
// SetClothingTint above, for a Hair Colour ColorEdit3 dragged every frame.
bool CharPreviewLibgodot_SetHairTint(float r, float g, float b);

// Texture RID's get_id() — передати напряму як ImTextureID у
// ImGui::Image() (той самий контракт, що imgui_impl_renderingserver.cpp's
// RenderDrawData() уже встановлює: RID::from_uint64((uint64_t)ImTextureID)
// напряму, без окремої ImTextureData-реєстрації для текстур, які ми самі
// створили через RenderingServer). Повертає 0 якщо ще не ініціалізовано.
uint64_t CharPreviewLibgodot_TextureId();
int      CharPreviewLibgodot_ViewportW();
int      CharPreviewLibgodot_ViewportH();
bool     CharPreviewLibgodot_IsLoaded();

// MUST run BEFORE LibGodotBridge::Shutdown()/window_shutdown() — той
// самий Ref<>-outliving-RS-usage bug class, що game/src/main_libgodot.
// cpp's npc_skin_ref/doc/state коментарі вже документують: doc/state тут
// теж тримаються живими у file-scope State (не local {}-блок) до цього
// виклику.
void CharPreviewLibgodot_Shutdown();

#endif // MD_USE_LIBGODOT
