#include <monkey_dust/ecs/component_reflect.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/components/sense_component.h>
#include <cstdio>
#include <cstring>

namespace md {

// ── FNV-1a 32-bit ─────────────────────────────────────────────────────────────
uint32_t ComponentReflect::Hash(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
    return h ? h : 1u;
}

// ── Singleton ─────────────────────────────────────────────────────────────────
ComponentReflect& ComponentReflect::Get() {
    static ComponentReflect inst;
    return inst;
}

// ── Register ──────────────────────────────────────────────────────────────────
bool ComponentReflect::Register(const char* name,
                                 const FieldDesc* fields, int field_count,
                                 uint16_t component_size) {
    if (!name || !fields || field_count <= 0) return false;
    if (count_ >= MAX_COMPONENTS) {
        fprintf(stderr, "[ComponentReflect] MAX_COMPONENTS=%d reached, skip '%s'\n",
                MAX_COMPONENTS, name);
        return false;
    }
    uint32_t h = Hash(name);
    for (int i = 0; i < count_; ++i)
        if (descs_[i].name_hash == h) return false;  // already registered

    ComponentDesc& d = descs_[count_++];
    strncpy(d.name, name, sizeof(d.name) - 1);
    d.name_hash      = h;
    d.fields         = fields;
    d.field_count    = (uint8_t)(field_count < 24 ? field_count : 24);
    d.component_size = component_size;
    return true;
}

// ── Find ──────────────────────────────────────────────────────────────────────
const ComponentDesc* ComponentReflect::Find(uint32_t h) const {
    for (int i = 0; i < count_; ++i)
        if (descs_[i].name_hash == h) return &descs_[i];
    return nullptr;
}

const ComponentDesc* ComponentReflect::Find(const char* name) const {
    return Find(Hash(name));
}

// ── Dump ──────────────────────────────────────────────────────────────────────
void ComponentReflect::Dump(const void* ptr, const ComponentDesc& d) {
    fprintf(stdout, "[%s] (%u bytes)\n", d.name, d.component_size);
    for (int i = 0; i < d.field_count; ++i) {
        const FieldDesc& f = d.fields[i];
        const uint8_t*   p = (const uint8_t*)ptr + f.offset;
        fprintf(stdout, "  %-20s = ", f.name);
        switch (f.type) {
        case FieldType::F32:   fprintf(stdout, "%.4f",   *(const float*)p);    break;
        case FieldType::I32:   fprintf(stdout, "%d",     *(const int32_t*)p);  break;
        case FieldType::U32:   fprintf(stdout, "%u",     *(const uint32_t*)p); break;
        case FieldType::U8:    fprintf(stdout, "%u",     (unsigned)*p);        break;
        case FieldType::U16:   fprintf(stdout, "%u",     *(const uint16_t*)p); break;
        case FieldType::U64:   fprintf(stdout, "%llu",   (unsigned long long)*(const uint64_t*)p); break;
        case FieldType::Bool:  fprintf(stdout, "%s",     *p ? "true" : "false"); break;
        case FieldType::Vec3: {
            const float* v = (const float*)p;
            fprintf(stdout, "(%.3f, %.3f, %.3f)", v[0], v[1], v[2]);
            break;
        }
        case FieldType::Enum8:  fprintf(stdout, "%u (enum)",  (unsigned)*p); break;
        case FieldType::Enum32: fprintf(stdout, "%u (enum)",  *(const uint32_t*)p); break;
        default: fprintf(stdout, "?"); break;
        }
        fprintf(stdout, "\n");
    }
}

// ── RegisterCoreComponents ────────────────────────────────────────────────────
// Reflects engine-owned components. Call once at startup after ECS init.

void RegisterCoreComponents() {
    auto& r = ComponentReflect::Get();

    // WorldTransform — spatial position + rotation (top-down RPG)
    static const FieldDesc wt_fields[] = {
        MD_FIELD(WorldTransform, x,     F32),
        MD_FIELD(WorldTransform, y,     F32),
        MD_FIELD(WorldTransform, z,     F32),
        MD_FIELD_RANGE(WorldTransform, rot_y, F32, 1.f, -180.f, 180.f),
        MD_FIELD(WorldTransform, slot,  U32),
    };
    r.Register("world_transform", wt_fields,
               (int)(sizeof(wt_fields) / sizeof(wt_fields[0])),
               (uint16_t)sizeof(WorldTransform));

    // SenseComponent — NPC sense activations (Visual..Background)
    static const FieldDesc sc_fields[] = {
        MD_FIELD(SenseComponent, cone_set_idx,       U8),
        MD_FIELD(SenseComponent, activation[0],      F32),  // Visual
        MD_FIELD(SenseComponent, last_activated_ms[0], U64), // Visual last triggered
        MD_FIELD(SenseComponent, last_known_x,       F32),
        MD_FIELD(SenseComponent, last_known_z,       F32),
    };
    r.Register("sense_component", sc_fields,
               (int)(sizeof(sc_fields) / sizeof(sc_fields[0])),
               (uint16_t)sizeof(SenseComponent));

    fprintf(stdout, "[ComponentReflect] %d components registered\n", r.Count());
}

} // namespace md
