#include <monkey_dust/world/transform_soa.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>
#include <cmath>

#ifdef __SSE2__
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

void TransformSoA::Init() {
    memset(slot_to_entity, 0xFF, sizeof(slot_to_entity)); // all = entt::null pattern
    for (int i = 0; i < MAX_SLOTS; ++i) {
        px[i]      = DUMMY_POS;
        pz[i]      = DUMMY_POS;
        py[i]      = 0.0f;
        rot_y[i]   = 0.0f;
        dist_sq[i] = 1e18f;
        faction[i] = 0;
    }
    active_count = 0;
    transform_ssbo_.Init(MAX_SLOTS * 4 * sizeof(float));
    faction_ssbo_.Init(MAX_SLOTS * sizeof(uint32_t));
}

uint32_t TransformSoA::Alloc(entt::entity e, float x, float z, uint8_t faction_id) {
    if (active_count >= MAX_SLOTS) {
        MD_LOG(MD_LOG_WARNING, "[TransformSoA] Alloc: out of slots (%d)", MAX_SLOTS);
        return INVALID_SLOT;
    }
    uint32_t slot = (uint32_t)active_count++;
    px[slot]      = x;
    pz[slot]      = z;
    py[slot]      = 0.0f;
    rot_y[slot]   = 0.0f;
    dist_sq[slot] = 1e18f;
    faction[slot] = faction_id;
    slot_to_entity[slot] = e;
    return slot;
}

void TransformSoA::Free(entt::entity e) {
    auto& reg = Registry::Get();
    if (!reg.valid(e) || !reg.all_of<WorldTransform>(e)) return;
    auto& tr  = reg.get<WorldTransform>(e);
    uint32_t slot = tr.slot;
    if (slot == INVALID_SLOT || slot >= (uint32_t)active_count) return;
    tr.slot = INVALID_SLOT;

    uint32_t last = (uint32_t)(--active_count);
    if (slot != last) {
        px[slot]      = px[last];
        pz[slot]      = pz[last];
        py[slot]      = py[last];
        rot_y[slot]   = rot_y[last];
        dist_sq[slot] = dist_sq[last];
        faction[slot] = faction[last];
        entt::entity moved = slot_to_entity[last];
        slot_to_entity[slot] = moved;
        if (reg.valid(moved) && reg.all_of<WorldTransform>(moved))
            reg.get<WorldTransform>(moved).slot = slot;
    }
    px[last]      = DUMMY_POS;
    pz[last]      = DUMMY_POS;
    slot_to_entity[last] = entt::null;
}

void TransformSoA::FlushAoStoSoA(entt::registry& reg) {
    reg.view<WorldTransform>().each([](const WorldTransform& tr) {
        if (tr.slot == INVALID_SLOT || tr.slot >= (uint32_t)TransformSoA::Get().active_count)
            return;
        auto& s = TransformSoA::Get();
        s.px[tr.slot]    = tr.x;
        s.pz[tr.slot]    = tr.z;
        s.py[tr.slot]    = tr.y;
        s.rot_y[tr.slot] = tr.rot_y;
    });
}

void TransformSoA::UploadToGPU() {
    if (active_count <= 0) return;
    static float    tf[MAX_SLOTS * 4];
    static uint32_t ff[MAX_SLOTS];
    for (int i = 0; i < active_count; ++i) {
        tf[i*4+0] = px[i];
        tf[i*4+1] = pz[i];
        tf[i*4+2] = py[i];
        tf[i*4+3] = rot_y[i];
        ff[i]     = (uint32_t)faction[i];
    }
    transform_ssbo_.Upload(tf, active_count * 4 * (int)sizeof(float), 0);
    faction_ssbo_.Upload(ff, active_count * (int)sizeof(uint32_t), 0);
}

void TransformSoA::BulkComputeDistSq(float cam_x, float cam_z) {
#ifdef __SSE2__
    __m128 cx4 = _mm_set1_ps(cam_x);
    __m128 cz4 = _mm_set1_ps(cam_z);
    int n4 = active_count & ~3;
    for (int i = 0; i < n4; i += 4) {
        __m128 dx = _mm_sub_ps(_mm_load_ps(px + i), cx4);
        __m128 dz = _mm_sub_ps(_mm_load_ps(pz + i), cz4);
        _mm_store_ps(dist_sq + i, _mm_add_ps(_mm_mul_ps(dx, dx), _mm_mul_ps(dz, dz)));
    }
    for (int i = n4; i < active_count; ++i) {
        float dx = px[i] - cam_x, dz = pz[i] - cam_z;
        dist_sq[i] = dx*dx + dz*dz;
    }
#else
    for (int i = 0; i < active_count; ++i) {
        float dx = px[i] - cam_x, dz = pz[i] - cam_z;
        dist_sq[i] = dx*dx + dz*dz;
    }
#endif
}

void TransformSoA::BulkComputeLOD(float near_sq, float far_sq, uint8_t* out_lod) const {
#ifdef __SSE2__
    __m128 near4 = _mm_set1_ps(near_sq);
    __m128 far4  = _mm_set1_ps(far_sq);
    int n4 = active_count & ~3;
    for (int i = 0; i < n4; i += 4) {
        __m128 d      = _mm_load_ps(dist_sq + i);
        int mask_n    = _mm_movemask_ps(_mm_cmplt_ps(d, near4));
        int mask_f    = _mm_movemask_ps(_mm_cmplt_ps(d, far4));
        for (int j = 0; j < 4; ++j)
            out_lod[i+j] = (mask_n >> j) & 1 ? 0 : ((mask_f >> j) & 1 ? 1 : 2);
    }
    for (int i = n4; i < active_count; ++i)
        out_lod[i] = dist_sq[i] < near_sq ? 0 : (dist_sq[i] < far_sq ? 1 : 2);
#else
    for (int i = 0; i < active_count; ++i)
        out_lod[i] = dist_sq[i] < near_sq ? 0 : (dist_sq[i] < far_sq ? 1 : 2);
#endif
}

int TransformSoA::BuildMatrices(const uint8_t* lod, float far_sq,
                                 Mat4* out_matrices, int max_out) const {
    int count = 0;
    for (int i = 0; i < active_count && count < max_out; ++i) {
        if (lod && lod[i] >= 2) continue;
        if (px[i] >= DUMMY_POS) continue;
        out_matrices[count++] = BuildSingleMatrix((uint32_t)i);
    }
    return count;
}

Mat4 TransformSoA::BuildSingleMatrix(uint32_t slot) const {
    return mat4_mul(mat4_rotate_y(rot_y[slot]),
                    mat4_translate(px[slot], py[slot] + 0.9f, pz[slot]));
}

// Save v5 accessors (БОРГ-6)
uint32_t TransformSoA::GetSlotForEntity(entt::entity e) const {
    for (int i = 0; i < active_count; ++i)
        if (slot_to_entity[i] == e) return (uint32_t)i;
    return INVALID_SLOT;
}

void TransformSoA::AssignSlot(entt::entity e, uint32_t slot,
                               float x, float z, uint8_t faction_id) {
    if (slot >= MAX_SLOTS) {
        MD_LOG(MD_LOG_WARNING, "[TransformSoA] AssignSlot: slot %u out of range", slot);
        return;
    }
    px[slot]    = x;
    pz[slot]    = z;
    py[slot]    = 0.0f;
    rot_y[slot] = 0.0f;
    dist_sq[slot] = 1e18f;
    faction[slot] = faction_id;
    slot_to_entity[slot] = e;
    if ((int)slot >= active_count) active_count = (int)slot + 1;
}
