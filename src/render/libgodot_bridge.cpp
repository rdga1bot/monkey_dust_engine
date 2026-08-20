#include <monkey_dust/render/libgodot_bridge.h>

#ifdef MD_USE_LIBGODOT

#include "servers/rendering/rendering_server.h"
#include "servers/display/display_server.h"
#include "core/math/transform_3d.h"
#include "core/templates/vector.h"

LibGodotBridge::LibGodotBridge() = default;

LibGodotBridge::~LibGodotBridge() {
    Shutdown();
}

bool LibGodotBridge::Init(int viewport_w, int viewport_h, bool attach_to_screen) {
    if (initialized_) return true;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;

    RID scenario = rs->scenario_create();
    RID camera   = rs->camera_create();
    rs->camera_set_perspective(camera, 60.0f, 0.05f, 4000.0f);

    RID viewport = rs->viewport_create();
    rs->viewport_set_scenario(viewport, scenario);
    rs->viewport_attach_camera(viewport, camera);
    rs->viewport_set_size(viewport, viewport_w, viewport_h);
    rs->viewport_set_active(viewport, true);
    rs->viewport_set_update_mode(viewport, RenderingServer::VIEWPORT_UPDATE_ALWAYS);
    if (attach_to_screen) {
        rs->viewport_attach_to_screen(viewport, Rect2(), DisplayServer::MAIN_WINDOW_ID);
    }
    // attach_to_screen=false: viewport renders off-screen only,
    // viewport_get_texture() still returns the composed frame (same
    // readback path probes/ already uses for screenshots) -- no claim on
    // the OS window SDL3 owns in a real in-game coexistence scenario.

    scenario_rid_id_ = scenario.get_id();
    camera_rid_id_   = camera.get_id();
    viewport_rid_id_ = viewport.get_id();
    initialized_ = true;
    return true;
}

void LibGodotBridge::Shutdown() {
    if (!initialized_) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (rs) {
        // Зворотній порядок створення: groups -> viewport -> camera -> scenario.
        for (auto it = groups_.rbegin(); it != groups_.rend(); ++it) {
            if (it->instance_rid_id)  rs->free_rid(RID::from_uint64(it->instance_rid_id));
            if (it->multimesh_rid_id) rs->free_rid(RID::from_uint64(it->multimesh_rid_id));
        }
        if (viewport_rid_id_) rs->free_rid(RID::from_uint64(viewport_rid_id_));
        if (camera_rid_id_)   rs->free_rid(RID::from_uint64(camera_rid_id_));
        if (scenario_rid_id_) rs->free_rid(RID::from_uint64(scenario_rid_id_));
    }
    groups_.clear();
    scenario_rid_id_ = camera_rid_id_ = viewport_rid_id_ = 0;
    initialized_ = false;
}

void LibGodotBridge::SetCameraTransform(float x, float y, float z,
                                         float look_x, float look_y, float look_z) {
    if (!initialized_ || !camera_rid_id_) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return;
    Vector3 eye(x, y, z);
    Vector3 target(look_x, look_y, look_z);
    Transform3D xf;
    if (eye.distance_squared_to(target) > 1e-8f) {
        xf.set_look_at(eye, target);
    } else {
        xf.origin = eye; // degenerate eye==target: keep identity basis, just place origin
    }
    rs->camera_set_transform(RID::from_uint64(camera_rid_id_), xf);
}

int LibGodotBridge::RegisterGroup(uint64_t mesh_rid_id, uint64_t material_rid_id, int max_instances,
                                   bool use_custom_data) {
    if (!initialized_ || max_instances <= 0) return -1;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return -1;

    RID multimesh = rs->multimesh_create();
    rs->multimesh_allocate_data(multimesh, max_instances, RenderingServer::MULTIMESH_TRANSFORM_3D,
                                 /*use_colors=*/false, use_custom_data);
    if (mesh_rid_id) {
        rs->multimesh_set_mesh(multimesh, RID::from_uint64(mesh_rid_id));
    }
    (void)material_rid_id; // призначення матеріалу -- на mesh-surface рівні, поза скоупом цієї фази

    RID instance = rs->instance_create();
    rs->instance_set_base(instance, multimesh);
    rs->instance_set_scenario(instance, RID::from_uint64(scenario_rid_id_));
    rs->instance_set_visible(instance, true);

    Group g;
    g.mesh_rid_id      = mesh_rid_id;
    g.material_rid_id  = material_rid_id;
    g.multimesh_rid_id = multimesh.get_id();
    g.instance_rid_id  = instance.get_id();
    g.capacity         = max_instances;
    g.use_custom_data  = use_custom_data;
    groups_.push_back(g);
    return (int)groups_.size() - 1;
}

bool LibGodotBridge::SyncGroupTransforms(int group_index, const float* xform_data, int visible_count) {
    if (group_index < 0 || group_index >= (int)groups_.size()) return false;
    Group& g = groups_[group_index];
    // Wrong stride (12 vs 16 floats/instance) if the group has
    // custom_data -- same silent-reject class of bug as the
    // short-buffer issue this class already found once; reject loudly
    // here instead of repeating that mistake.
    if (g.use_custom_data) return false;
    if (visible_count < 0 || visible_count > g.capacity) return false;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;

    // ОДИН multimesh_set_buffer() виклик -- CONSTRAINT з плану Фази B.1:
    // категорично заборонено instance_set_transform() у циклі per-entity.
    // ВАЖЛИВО (знайдено емпірично через реальний ERR_FAIL_COND у
    // mesh_storage.cpp _multimesh_set_buffer -- перша версія цього коду
    // мовчки "успішно" повертала true для коротшого буфера, хоча Godot
    // сам відкидав виклик): multimesh_set_buffer() вимагає буфер РІВНО
    // capacity*12 floats щоразу, без часткового upload. Видиму
    // підмножину контролює ОКРЕМИЙ виклик multimesh_set_visible_instances().
    RID mm = RID::from_uint64(g.multimesh_rid_id);
    Vector<float> buffer;
    buffer.resize(g.capacity * 12);
    if (g.capacity > 0) {
        memcpy(buffer.ptrw(), xform_data, sizeof(float) * g.capacity * 12);
    }
    rs->multimesh_set_buffer(mm, buffer);
    rs->multimesh_set_visible_instances(mm, visible_count);
    return true;
}

bool LibGodotBridge::SyncGroupTransformsAndCustomData(int group_index, const float* xform_data,
                                                       const float* custom_data, int visible_count) {
    if (group_index < 0 || group_index >= (int)groups_.size()) return false;
    Group& g = groups_[group_index];
    if (!g.use_custom_data) return false;
    if (visible_count < 0 || visible_count > g.capacity) return false;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;

    // Godot's own per-instance layout when use_custom_data=true (see
    // mesh_storage.cpp: color_offset_cache=12, custom_data_offset_cache=
    // color_offset_cache (no colors here), stride_cache=custom_data_
    // offset_cache+4) -- INTERLEAVED, not two separate buffers: each
    // instance is [12 floats transform][4 floats custom_data], stride=16.
    RID mm = RID::from_uint64(g.multimesh_rid_id);
    Vector<float> buffer;
    buffer.resize(g.capacity * 16);
    float* dst = buffer.ptrw();
    for (int i = 0; i < g.capacity; ++i) {
        memcpy(dst + (size_t)i * 16,      xform_data  + (size_t)i * 12, sizeof(float) * 12);
        memcpy(dst + (size_t)i * 16 + 12, custom_data + (size_t)i * 4,  sizeof(float) * 4);
    }
    rs->multimesh_set_buffer(mm, buffer);
    rs->multimesh_set_visible_instances(mm, visible_count);
    return true;
}

int LibGodotBridge::GetGroupInstanceCapacity(int group_index) const {
    if (group_index < 0 || group_index >= (int)groups_.size()) return -1;
    return groups_[group_index].capacity;
}

#endif // MD_USE_LIBGODOT
