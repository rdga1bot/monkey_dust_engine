#include <monkey_dust/render/char_preview_libgodot.h>

#ifdef MD_USE_LIBGODOT

#include "servers/rendering/rendering_server.h"
#include "core/math/transform_3d.h"
#include "modules/gltf/gltf_document.h"
#include "modules/gltf/gltf_state.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/mesh.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

// Витягнуто зі spike-проби тексту, що game/src/main_libgodot.cpp's NPC-
// loading блок і probes/libgodot_integration_loop_test.cpp's boulder
// preview обидва вже live-verified: GLTFDocument::append_from_file() +
// generate_scene() + перший MeshInstance3D у дереві + AABB-based camera
// framing (center=bb.position+bb.size*0.5, dist~=bb.size.length()).
// Жодного нового підходу тут не винайдено — лише зібрано в один
// character-preview-специфічний модуль.

namespace {

static MeshInstance3D* FindFirstMeshInstance(Node* n) {
    if (!n) return nullptr;
    MeshInstance3D* mi = Object::cast_to<MeshInstance3D>(n);
    if (mi) return mi;
    for (int i = 0; i < n->get_child_count(); ++i) {
        MeshInstance3D* found = FindFirstMeshInstance(n->get_child(i));
        if (found) return found;
    }
    return nullptr;
}

// Parses a real .clothbin (see char_preview_libgodot.h's own doc comment
// on CharPreviewLibgodot_SetClothingSlot for the exact byte layout — same
// format tools/editor/editor_char_preview_assets.cpp's LoadClothingSlot()
// already reads for the SDL3 path) into a static ArrayMesh. Position +
// normal only (no skinning applied — see header doc comment for why
// that's correct, not a shortcut). Returns an invalid Ref<> on any parse
// failure (missing file / bad magic / implausible counts), logged to
// stderr with the same "[Cloth]"-prefixed message shape the SDL3 loader
// uses so log-grepping tooling shared across both backends still works.
static Ref<ArrayMesh> LoadClothbinMesh(const String& path) {
    CharString path_utf8 = path.utf8();
    FILE* fp = fopen(path_utf8.get_data(), "rb");
    if (!fp) {
        fprintf(stderr, "[CharPreviewLibgodot][Cloth] missing: %s\n", path_utf8.get_data());
        return Ref<ArrayMesh>();
    }
    uint32_t hdr[4];
    if (fread(hdr, 4, 4, fp) != 4) {
        fclose(fp);
        fprintf(stderr, "[CharPreviewLibgodot][Cloth] truncated header: %s\n", path_utf8.get_data());
        return Ref<ArrayMesh>();
    }
    if (hdr[0] != 0x544F4C43u) { // 'COLT'
        fclose(fp);
        fprintf(stderr, "[CharPreviewLibgodot][Cloth] bad magic: %s\n", path_utf8.get_data());
        return Ref<ArrayMesh>();
    }
    uint32_t nv = hdr[1], ni = hdr[2], flags = hdr[3];
    bool idx32 = (flags & 1u) != 0u;
    if (nv == 0 || nv > 20000 || ni == 0 || ni > 200000) {
        fclose(fp);
        fprintf(stderr, "[CharPreviewLibgodot][Cloth] implausible counts (nv=%u ni=%u): %s\n",
                nv, ni, path_utf8.get_data());
        return Ref<ArrayMesh>();
    }

    std::vector<uint8_t> vbuf((size_t)nv * 52);
    if (fread(vbuf.data(), 52, nv, fp) != nv) {
        fclose(fp);
        fprintf(stderr, "[CharPreviewLibgodot][Cloth] truncated verts: %s\n", path_utf8.get_data());
        return Ref<ArrayMesh>();
    }
    size_t ibytes = (size_t)ni * (idx32 ? 4u : 2u);
    std::vector<uint8_t> ibuf(ibytes);
    if (fread(ibuf.data(), 1, ibytes, fp) != ibytes) {
        fclose(fp);
        fprintf(stderr, "[CharPreviewLibgodot][Cloth] truncated indices: %s\n", path_utf8.get_data());
        return Ref<ArrayMesh>();
    }
    fclose(fp);

    PackedVector3Array verts;   verts.resize((int)nv);
    PackedVector3Array normals; normals.resize((int)nv);
    for (uint32_t i = 0; i < nv; ++i) {
        const uint8_t* v = vbuf.data() + (size_t)i * 52;
        float p[3], n[3];
        memcpy(p, v + 0,  12);
        memcpy(n, v + 12, 12);
        verts.set((int)i,   Vector3(p[0], p[1], p[2]));
        normals.set((int)i, Vector3(n[0], n[1], n[2]));
    }
    PackedInt32Array indices; indices.resize((int)ni);
    for (uint32_t i = 0; i < ni; ++i) {
        uint32_t idx;
        if (idx32) {
            memcpy(&idx, ibuf.data() + (size_t)i * 4, 4);
        } else {
            uint16_t v16;
            memcpy(&v16, ibuf.data() + (size_t)i * 2, 2);
            idx = v16;
        }
        indices.set((int)i, (int32_t)idx);
    }

    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = verts;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_INDEX]  = indices;

    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return mesh;
}

constexpr int kPreviewW = 420;
constexpr int kPreviewH = 560;

// Same unshaded+tint shader shape as game/src/main_libgodot.cpp's
// kUnshadedShaderSrc (live-verified: ALBEDO = tint.rgb) — reused, not
// reinvented. No lighting/environment setup needed for this preview
// since render_mode is unshaded.
static const char* kPreviewShaderSrc = R"(
shader_type spatial;
render_mode unshaded, cull_disabled;
uniform vec4 tint : source_color = vec4(0.82, 0.65, 0.52, 1.0);
void fragment() { ALBEDO = tint.rgb; }
)";

// Per-slot clothing state — mesh is an owned Ref<ArrayMesh> (a RefCounted
// Resource, not a Node), so its lifetime is trivial compared to the hair/
// body GLTFDocument path below: dropping the Ref frees the mesh's own RS
// RID automatically once nothing else holds it. The ONLY ordering rule
// that still applies (same class of bug as CharPreviewLibgodot_Shutdown's
// own doc comment): free `instance` (which references the mesh) before
// resetting `mesh`, and never free `material` while any mesh surface
// still points at it.
struct ClothSlotState {
    RID instance;
    RID material;
    Ref<ArrayMesh> mesh;
    bool loaded = false;
};

// Hair uses the same GLTFDocument::append_from_file+generate_scene path
// as the body mesh (real .glb, not a raw-triangle format like clothing),
// so it needs the same doc/gstate/scene_root-outlives-material discipline
// the body's own State fields already document above.
struct HairState {
    RID instance;
    RID material;
    Ref<GLTFDocument> doc;
    Ref<GLTFState>    gstate;
    Node* scene_root = nullptr;
    bool loaded = false;
};

struct State {
    bool initialized = false;

    RID scenario, camera, viewport;
    RID shader, material, mesh_instance;

    // Ref<>-outliving-RS-usage: doc/state MUST stay alive at this
    // file-scope State (not a local {}-block) until CharPreviewLibgodot_
    // Shutdown() runs — same bug class game/src/main_libgodot.cpp's own
    // doc/state comments document (GLTFState owns the Mesh/Material/
    // Texture cache; an early destructor call yields "Parameter material
    // is null" on every subsequent frame).
    Ref<GLTFDocument> doc;
    Ref<GLTFState>    gstate;
    Node* scene_root = nullptr;

    ClothSlotState cloth[kCharPreviewClothSlots];
    HairState      hair;

    Vector3 center{0.f, 0.f, 0.f};
    float   base_dist = 3.f;
    float   yaw   = 0.f;   // radians
    float   pitch = 12.f;  // degrees
    float   dist  = 3.f;
};

State& S() {
    static State s;
    return s;
}

} // namespace

bool CharPreviewLibgodot_Init(const std::string& launch_cwd) {
    State& s = S();
    if (s.initialized) return true;

    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;

    s.scenario = rs->scenario_create();
    s.camera   = rs->camera_create();
    rs->camera_set_perspective(s.camera, 35.0f, 0.02f, 100.0f);

    s.viewport = rs->viewport_create();
    rs->viewport_set_scenario(s.viewport, s.scenario);
    rs->viewport_attach_camera(s.viewport, s.camera);
    rs->viewport_set_size(s.viewport, kPreviewW, kPreviewH);
    rs->viewport_set_active(s.viewport, true);
    rs->viewport_set_update_mode(s.viewport, RenderingServer::VIEWPORT_UPDATE_ALWAYS);
    // attach_to_screen intentionally NEVER called here: this viewport is
    // offscreen-only, displayed via ImGui::Image(viewport_get_texture())
    // — same read-back path probes/libgodot_integration_loop_test.cpp's
    // screenshot already proves works on a headless (attach_to_screen=
    // false) viewport.

    s.shader   = rs->shader_create_from_code(kPreviewShaderSrc);
    s.material = rs->material_create_from_shader(RID(), 0, s.shader);

    String glb_path = String((launch_cwd + "/game/data/props/md_human.glb").c_str());
    s.doc.instantiate();
    s.gstate.instantiate();
    if (s.doc->append_from_file(glb_path, s.gstate) != OK) {
        fprintf(stderr, "[CharPreviewLibgodot] FAILED: GLTFDocument::append_from_file('%s')\n",
                glb_path.utf8().get_data());
        return false;
    }
    s.scene_root = s.doc->generate_scene(s.gstate);
    MeshInstance3D* mi = s.scene_root ? FindFirstMeshInstance(s.scene_root) : nullptr;
    Ref<Mesh> mesh = mi ? mi->get_mesh() : Ref<Mesh>();
    if (!mesh.is_valid()) {
        fprintf(stderr, "[CharPreviewLibgodot] FAILED: no MeshInstance3D/Mesh in '%s'\n",
                glb_path.utf8().get_data());
        if (s.scene_root) { memdelete(s.scene_root); s.scene_root = nullptr; }
        return false;
    }

    RID mesh_rid = mesh->get_rid();
    int surf_count = mesh->get_surface_count();
    for (int i = 0; i < surf_count; ++i)
        rs->mesh_surface_set_material(mesh_rid, i, s.material);

    AABB bb = mesh->get_aabb();
    s.center    = bb.position + bb.size * 0.5f;
    s.base_dist = bb.size.length() * 0.9f;
    if (s.base_dist < 0.5f) s.base_dist = 0.5f;
    s.dist = s.base_dist;

    s.mesh_instance = rs->instance_create();
    rs->instance_set_base(s.mesh_instance, mesh_rid);
    rs->instance_set_scenario(s.mesh_instance, s.scenario);
    rs->instance_set_visible(s.mesh_instance, true);

    // Clothing/hair materials: one per slot, created once up-front and
    // freed only in Shutdown() (same pattern as s.material above) — swap
    // functions below only ever touch instance/mesh, never recreate these,
    // so a "None" -> item -> "None" cycle never needs a new shader
    // compile. Each is independently tinted (own colour per item/slot),
    // reusing the SAME kPreviewShaderSrc as the skin material.
    for (int i = 0; i < kCharPreviewClothSlots; ++i)
        s.cloth[i].material = rs->material_create_from_shader(RID(), 0, s.shader);
    s.hair.material = rs->material_create_from_shader(RID(), 0, s.shader);

    s.initialized = true;
    CharPreviewLibgodot_Update(0.f, false, 0.f, 0.f, 0.f); // place camera before first frame
    printf("[CharPreviewLibgodot] OK: loaded '%s' (%d surfaces, center=(%.2f,%.2f,%.2f), dist=%.2f)\n",
           glb_path.utf8().get_data(), surf_count, s.center.x, s.center.y, s.center.z, s.base_dist);
    return true;
}

void CharPreviewLibgodot_Update(float dt, bool dragging, float drag_dx, float drag_dy, float zoom_delta) {
    State& s = S();
    if (!s.initialized) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return;

    if (dragging) {
        // RMB/LMB-drag orbit — negate both deltas to match the standard
        // orbit-camera convention already used by tools/editor/main_
        // libgodot.cpp's OrbitCamera and the SDL_GPU reference
        // (editor_char_preview_runtime.cpp: s_yaw -= MouseDelta.x*0.007).
        s.yaw   -= drag_dx * 0.01f;
        s.pitch += drag_dy * 0.15f;
    } else {
        // Kenshi-style idle portrait auto-rotate when not dragging (see
        // this function's own header doc comment) — simplified to a
        // continuous slow spin instead of the SDL_GPU reference's
        // ±45° oscillation window.
        s.yaw += dt * 0.25f;
    }
    if (s.pitch > 80.f)  s.pitch = 80.f;
    if (s.pitch < -80.f) s.pitch = -80.f;

    s.dist -= zoom_delta;
    float min_dist = s.base_dist * 0.4f;
    float max_dist = s.base_dist * 3.0f;
    if (s.dist < min_dist) s.dist = min_dist;
    if (s.dist > max_dist) s.dist = max_dist;

    constexpr float kDeg2Rad = 3.14159265f / 180.f;
    float pitch_r = s.pitch * kDeg2Rad;
    Vector3 eye = s.center + Vector3(
        s.dist * cosf(pitch_r) * sinf(s.yaw),
        s.dist * sinf(pitch_r) + s.base_dist * 0.15f,
        s.dist * cosf(pitch_r) * cosf(s.yaw));

    Transform3D xf;
    if (eye.distance_squared_to(s.center) > 1e-8f) {
        xf.set_look_at(eye, s.center);
    } else {
        xf.origin = eye;
    }
    rs->camera_set_transform(s.camera, xf);
}

void CharPreviewLibgodot_SetSkinColor(float r, float g, float b) {
    State& s = S();
    if (!s.initialized) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return;
    rs->material_set_param(s.material, "tint", Color(r, g, b, 1.0f));
}

bool CharPreviewLibgodot_SetClothingSlot(int slot, const char* clothbin_path,
                                          float r, float g, float b) {
    State& s = S();
    if (!s.initialized) return false;
    if (slot < 0 || slot >= kCharPreviewClothSlots) return false;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;

    ClothSlotState& cs = s.cloth[slot];
    // Free the OLD instance BEFORE dropping the OLD mesh Ref — same
    // release-dependents-before-the-resource rule as Shutdown()'s own doc
    // comment below (the instance references the mesh RID; the mesh's
    // Ref<> destructor frees that RID once nothing else points at it).
    if (cs.instance.is_valid()) { rs->free_rid(cs.instance); cs.instance = RID(); }
    cs.mesh = Ref<ArrayMesh>();
    cs.loaded = false;

    // Tint applies even on a clear, so the slot shows the right colour
    // the moment something is next equipped into it.
    rs->material_set_param(cs.material, "tint", Color(r, g, b, 1.0f));

    if (!clothbin_path) return true; // "None" — slot cleared successfully

    Ref<ArrayMesh> mesh = LoadClothbinMesh(String(clothbin_path));
    if (!mesh.is_valid()) return false;

    RID mesh_rid = mesh->get_rid();
    // mesh_surface_set_material (not instance_geometry_set_material_
    // override) is correct here — unlike libgodot_terrain_renderer.cpp's
    // per-node terrain mesh, this ArrayMesh is NOT shared across multiple
    // instances (one clothbin -> one Ref<ArrayMesh> -> one instance), so
    // there's no "every node shares the same material" bug class to avoid.
    rs->mesh_surface_set_material(mesh_rid, 0, cs.material);

    cs.instance = rs->instance_create();
    rs->instance_set_base(cs.instance, mesh_rid);
    rs->instance_set_scenario(cs.instance, s.scenario);
    rs->instance_set_visible(cs.instance, true);
    cs.mesh = mesh;
    cs.loaded = true;
    printf("[CharPreviewLibgodot][Cloth] slot%d: %s\n", slot, clothbin_path);
    return true;
}

bool CharPreviewLibgodot_SetClothingTint(int slot, float r, float g, float b) {
    State& s = S();
    if (!s.initialized) return false;
    if (slot < 0 || slot >= kCharPreviewClothSlots) return false;
    if (!s.cloth[slot].loaded) return false;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;
    rs->material_set_param(s.cloth[slot].material, "tint", Color(r, g, b, 1.0f));
    return true;
}

bool CharPreviewLibgodot_SetHairTint(float r, float g, float b) {
    State& s = S();
    if (!s.initialized) return false;
    if (!s.hair.loaded) return false;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;
    rs->material_set_param(s.hair.material, "tint", Color(r, g, b, 1.0f));
    return true;
}

bool CharPreviewLibgodot_SetHairStyle(const char* hair_glb_path, float r, float g, float b) {
    State& s = S();
    if (!s.initialized) return false;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;

    HairState& h = s.hair;
    // Same ordering as CharPreviewLibgodot_Shutdown(): instance (a
    // dependent of the mesh, via scene_root) freed first, then the node
    // tree/doc/gstate that own the previous style's Mesh resource.
    if (h.instance.is_valid()) { rs->free_rid(h.instance); h.instance = RID(); }
    if (h.scene_root) { memdelete(h.scene_root); h.scene_root = nullptr; }
    h.doc    = Ref<GLTFDocument>();
    h.gstate = Ref<GLTFState>();
    h.loaded = false;

    rs->material_set_param(h.material, "tint", Color(r, g, b, 1.0f));

    if (!hair_glb_path) return true; // hair removed

    String path = String(hair_glb_path);
    h.doc.instantiate();
    h.gstate.instantiate();
    if (h.doc->append_from_file(path, h.gstate) != OK) {
        fprintf(stderr, "[CharPreviewLibgodot][Hair] FAILED: append_from_file('%s')\n",
                path.utf8().get_data());
        h.doc = Ref<GLTFDocument>();
        h.gstate = Ref<GLTFState>();
        return false;
    }
    h.scene_root = h.doc->generate_scene(h.gstate);
    MeshInstance3D* mi = h.scene_root ? FindFirstMeshInstance(h.scene_root) : nullptr;
    Ref<Mesh> mesh = mi ? mi->get_mesh() : Ref<Mesh>();
    if (!mesh.is_valid()) {
        fprintf(stderr, "[CharPreviewLibgodot][Hair] FAILED: no MeshInstance3D/Mesh in '%s'\n",
                path.utf8().get_data());
        if (h.scene_root) { memdelete(h.scene_root); h.scene_root = nullptr; }
        h.doc = Ref<GLTFDocument>();
        h.gstate = Ref<GLTFState>();
        return false;
    }

    RID mesh_rid = mesh->get_rid();
    int surf_count = mesh->get_surface_count();
    for (int i = 0; i < surf_count; ++i)
        rs->mesh_surface_set_material(mesh_rid, i, h.material);

    h.instance = rs->instance_create();
    rs->instance_set_base(h.instance, mesh_rid);
    rs->instance_set_scenario(h.instance, s.scenario);
    rs->instance_set_visible(h.instance, true);
    h.loaded = true;
    printf("[CharPreviewLibgodot][Hair] OK: %s (%d surfaces)\n", path.utf8().get_data(), surf_count);
    return true;
}

uint64_t CharPreviewLibgodot_TextureId() {
    State& s = S();
    if (!s.initialized) return 0;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return 0;
    RID tex = rs->viewport_get_texture(s.viewport);
    return tex.get_id();
}

int CharPreviewLibgodot_ViewportW() { return kPreviewW; }
int CharPreviewLibgodot_ViewportH() { return kPreviewH; }
bool CharPreviewLibgodot_IsLoaded() { return S().initialized; }

void CharPreviewLibgodot_Shutdown() {
    State& s = S();
    if (!s.initialized) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (rs) {
        // Instance -> viewport -> camera -> scenario first (none of these
        // depend on the material RID for their own teardown). Cloth/hair
        // instances freed in the same batch, same reasoning — they
        // reference their own mesh/material, not each other.
        if (s.mesh_instance.is_valid()) rs->free_rid(s.mesh_instance);
        for (int i = 0; i < kCharPreviewClothSlots; ++i)
            if (s.cloth[i].instance.is_valid()) rs->free_rid(s.cloth[i].instance);
        if (s.hair.instance.is_valid()) rs->free_rid(s.hair.instance);
        if (s.viewport.is_valid())      rs->free_rid(s.viewport);
        if (s.camera.is_valid())        rs->free_rid(s.camera);
        if (s.scenario.is_valid())      rs->free_rid(s.scenario);
    }
    // scene_root/doc/gstate MUST be released BEFORE the material RID —
    // live-verified (not a guess): freeing s.material first produced four
    // "Parameter material is null" RS errors during shutdown, because
    // memdelete(scene_root) below still walks the Mesh resource's
    // per-surface material dependency (set via mesh_surface_set_material
    // in Init()) while tearing it down. Same Ref<>-outliving-RS-usage bug
    // class this file's own header doc comment already calls out for doc/
    // gstate — the fix generalizes to "free RIDs OTHER resources still
    // reference last", not just "keep Ref<> alive until here".
    if (s.scene_root) { memdelete(s.scene_root); s.scene_root = nullptr; }
    s.doc    = Ref<GLTFDocument>();
    s.gstate = Ref<GLTFState>();

    // Hair's scene_root/doc/gstate — same rule, same reason.
    if (s.hair.scene_root) { memdelete(s.hair.scene_root); s.hair.scene_root = nullptr; }
    s.hair.doc    = Ref<GLTFDocument>();
    s.hair.gstate = Ref<GLTFState>();

    // Clothing meshes are plain Ref<ArrayMesh> (RefCounted Resources, no
    // Node tree) — resetting the Ref frees the mesh's own RS RID via its
    // destructor. Safe here because the instances that referenced them
    // were already freed above, and the materials they point at (freed
    // below) are still valid at this point.
    for (int i = 0; i < kCharPreviewClothSlots; ++i)
        s.cloth[i].mesh = Ref<ArrayMesh>();

    if (rs) {
        for (int i = 0; i < kCharPreviewClothSlots; ++i)
            if (s.cloth[i].material.is_valid()) rs->free_rid(s.cloth[i].material);
        if (s.hair.material.is_valid()) rs->free_rid(s.hair.material);
        if (s.material.is_valid()) rs->free_rid(s.material);
        if (s.shader.is_valid())   rs->free_rid(s.shader);
    }
    s.initialized = false;
}

#endif // MD_USE_LIBGODOT
