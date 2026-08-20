#pragma once
// LibGodot migration Фаза B.1 — core glue layer.
// Компілюється лише коли MD_USE_LIBGODOT визначено (engine/CMakeLists.txt,
// гейтовано USE_LIBGODOT+LIBGODOT_ROOT). Порожня трансляційна одиниця
// інакше — engine/ і далі компілюється й лінкується без libgodot для
// звичайного USE_SDL3-шляху (dual-run, Фаза A CONSTRAINT).
#ifdef MD_USE_LIBGODOT

#include <cstdint>
#include <vector>

// Публічний API навмисно оперує uint64_t (RID::get_id()/RID::from_uint64()),
// не реальним godot::RID типом -- цей заголовок можна включати з коду, що
// НЕ хоче тягнути core/templates/rid.h напряму.

// Власник RenderingServer RID lifecycle (scenario/camera/viewport) +
// групування MultiMesh по (mesh, material) для TransformSoA-синку.
//
// CONSTRAINT (docs/LIBGODOT_MIGRATION_PLAN_PHASE_A_F.md, Фаза B.1):
//   - Shutdown() ОБОВ'ЯЗКОВО free_rid() у зворотньому порядку створення
//   - TransformSoA -> MultiMesh: ОДИН rs->multimesh_set_buffer() виклик,
//     КАТЕГОРИЧНО заборонено per-entity instance_set_transform() у циклі
//   - Групування по mesh/матеріалу: ОДИН MultiMesh RID на унікальну пару,
//     НЕ один на весь світ
//
// НЕ РЕАЛІЗОВАНО цією фазою (свідомо, не мовчки): AnimationSoA ->
// Skeleton3D glue. Фаза B.0 (docs/LIBGODOT_SKELETON3D_SPIKE_RESULTS.md)
// підтвердила: (1) немає batched skeleton-API (лише per-bone
// skeleton_bone_set_transform()), (2) worst-case 500x64 кісток коштує
// 1.8-4.0мс/кадр (11-24% бюджету), (3) поточний AnimationSoA вже виводить
// ГОТОВИЙ flat mat4[500][64] буфер через CPU LBS (OzzAnimator) одним
// UploadBonesInCmd() викликом -- перехід на Skeleton3D API замінив би
// один bulk-виклик на 32000 дрібних, що суперечить напрямку решти цього
// класу. Рекомендований шлях (не реалізовано тут): custom shader
// material зі storage-buffer binding, що читає AnimationSoA's існуючий
// bones_ssbo_ напряму -- Фаза C/D (shader-рівень), не тут.
class LibGodotBridge {
public:
    LibGodotBridge();
    ~LibGodotBridge();

    LibGodotBridge(const LibGodotBridge&) = delete;
    LibGodotBridge& operator=(const LibGodotBridge&) = delete;

    // Створює scenario/camera/viewport. false якщо RenderingServer
    // недоступний (виклик до libgodot_create_godot_instance()/start()).
    bool Init(int viewport_w, int viewport_h);
    // free_rid() у зворотньому порядку створення (groups -> viewport ->
    // camera -> scenario). Безпечно викликати повторно.
    void Shutdown();

    void SetCameraTransform(float x, float y, float z,
                             float look_x, float look_y, float look_z);

    // Реєструє нову (mesh, material) групу. mesh_id/material_id -- вже
    // створені RID-и (тут лише зберігаються, lifecycle власника -- caller,
    // за винятком самого MultiMesh RID, який належить цьому класу).
    // use_custom_data=true додає vec4/instance (Godot use_custom_data),
    // потрібно для per-instance даних, що НЕ вкладаються в transform
    // matrix (наприклад PropRenderer's anim_params: time/mode/mesh_height/
    // phase, Фаза C.5 Група 3 -- див. SyncGroupTransformsAndCustomData).
    // Повертає індекс групи ( >=0 ) або -1 при помилці.
    int RegisterGroup(uint64_t mesh_rid_id, uint64_t material_rid_id, int max_instances,
                       bool use_custom_data = false);
    int GroupCount() const { return (int)groups_.size(); }

    // ОДИН rs->multimesh_set_buffer() виклик на групу. xform_data --
    // MULTIMESH_TRANSFORM_3D формат (12 floats/instance), РІВНО
    // capacity*12 floats (реальний Godot API вимагає буфер, розміром
    // точно рівний allocate_data()'s instance count -- ЖОДНОГО часткового
    // upload через коротший буфер; підтверджено дослідно: коротший
    // буфер не падає, а МОВЧКИ ігнорується всередині
    // _multimesh_set_buffer() з лише ERR_FAIL_COND логом, і раніша
    // версія цього API тут це не перевіряла). visible_count --
    // скільки з перших інстансів реально рендериться цей кадр
    // (rs->multimesh_set_visible_instances(), окремий виклик,
    // <= capacity з RegisterGroup); хвостові неактивні слоти в
    // xform_data все одно мають бути заповнені (значення байдужі,
    // бо не рендеряться, але буфер має бути повного розміру).
    bool SyncGroupTransforms(int group_index, const float* xform_data, int visible_count);

    // Той самий контракт, що SyncGroupTransforms (xform_data ЗАВЖДИ
    // capacity*12 floats, visible_count -- окремий
    // multimesh_set_visible_instances() виклик), ПЛЮС custom_data --
    // ЗАВЖДИ capacity*4 floats (vec4/instance). Група МАЄ бути
    // зареєстрована з use_custom_data=true (RegisterGroup), інакше
    // повертає false. ОДИН multimesh_set_buffer() виклик на групу --
    // той самий CONSTRAINT, буфер лише ширший (16 floats/instance:
    // 12 transform + 4 custom_data, Godot's власний layout, не
    // окремий виклик на custom_data).
    bool SyncGroupTransformsAndCustomData(int group_index, const float* xform_data,
                                           const float* custom_data, int visible_count);

    int GetGroupInstanceCapacity(int group_index) const;

private:
    struct Group {
        uint64_t mesh_rid_id     = 0;
        uint64_t material_rid_id = 0;
        uint64_t multimesh_rid_id = 0; // власний RID, звільняється у Shutdown()
        uint64_t instance_rid_id  = 0; // instance_create(), base=multimesh
        int      capacity = 0;
        bool     use_custom_data = false;
    };

    uint64_t scenario_rid_id_ = 0;
    uint64_t camera_rid_id_   = 0;
    uint64_t viewport_rid_id_ = 0;
    bool     initialized_ = false;
    std::vector<Group> groups_;
};

#endif // MD_USE_LIBGODOT
