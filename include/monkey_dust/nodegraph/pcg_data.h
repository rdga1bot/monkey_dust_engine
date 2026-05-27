#pragma once
#include <cstdint>
#include <cstring>

namespace md {

enum class PcgDataType : uint8_t {
    None=0, HeightMap, Mask, Float, Vec3, PointCloud, BiomeMap, Curve
};

struct ScatterPoint {
    float    x, z, scale, rot_y;
    uint16_t prefab_id;
    uint8_t  _pad[2];
};  // 20 bytes

static constexpr int PCG_MAX_SCATTER = 4096;
static constexpr int PCG_FIELD_RES   = 256;   // materialized grid side

// ── FieldFn — lazy-evaluated function, no heap ────────────────────────────────
// Trivially constructible (no default-member-initializers) so it can live in a union.
struct FieldFn {
    float (*evaluate)(void* ctx, float x, float z);
    void*  ctx;

    bool valid() const { return evaluate != nullptr; }

    float operator()(float x, float z) const {
        return evaluate ? evaluate(ctx, x, z) : 0.f;
    }

    void Materialize(float* out, int W, int H, float world_size) const {
        if (!evaluate) { memset(out, 0, W * H * sizeof(float)); return; }
        float cell = world_size / (float)W;
        for (int z = 0; z < H; ++z)
            for (int x = 0; x < W; ++x)
                out[z * W + x] = evaluate(ctx, x * cell, z * cell);
    }

    static FieldFn Null() { FieldFn f; f.evaluate = nullptr; f.ctx = nullptr; return f; }
};

// ── PcgData — universal pin value ────────────────────────────────────────────
// Pointers reference static arenas owned by PcgGraph — never free them.
// Union contains FieldFn (trivially constructible) so we provide explicit ctor.
struct PcgData {
    PcgDataType  type;
    uint8_t      _pad[3];
    uint32_t     version;  // bumped each Execute(); upstream cache key

    union {
        float*        height_ptr;   // HeightMap: PCG_FIELD_RES² floats
        float*        mask_ptr;     // Mask:      PCG_FIELD_RES² floats
        float         scalar;       // Float
        float         vec3[3];      // Vec3
        ScatterPoint* scatter_ptr;  // PointCloud: up to PCG_MAX_SCATTER
        uint8_t*      biome_ptr;    // BiomeMap: PCG_FIELD_RES² uint8
        FieldFn       field;        // lazy HeightMap / Mask (P-NG-4)
    };
    int count;  // scatter: point count; height/mask: always PCG_FIELD_RES²

    PcgData() : type(PcgDataType::None), _pad{}, version(0), height_ptr(nullptr), count(0) {}

    static PcgData MakeFloat(float v, uint32_t ver = 0) {
        PcgData d; d.type = PcgDataType::Float; d.scalar = v; d.version = ver; return d;
    }
    static PcgData MakeField(FieldFn fn, uint32_t ver = 0) {
        PcgData d; d.type = PcgDataType::HeightMap; d.field = fn; d.version = ver; return d;
    }
    static PcgData MakeNone() { return PcgData{}; }

    bool IsNone() const { return type == PcgDataType::None; }
};

// ── Type compatibility check for pin connections ───────────────────────────────
// Returns true if `from` can connect to `to`.
inline bool PcgPinCompatible(PcgDataType from, PcgDataType to) {
    if (to == PcgDataType::None) return true;   // sink accepts anything
    if (from == to)              return true;
    // HeightMap ↔ Mask are interchangeable (both float grids)
    if ((from == PcgDataType::HeightMap || from == PcgDataType::Mask) &&
        (to   == PcgDataType::HeightMap || to   == PcgDataType::Mask))
        return true;
    return false;
}

// ── Pin type → display color (RGBA float) ─────────────────────────────────────
struct PinColor { float r,g,b,a; };
inline PinColor PcgPinColor(PcgDataType t) {
    switch (t) {
    case PcgDataType::HeightMap:  return {0.3f, 0.9f, 0.4f, 1.f};  // green
    case PcgDataType::Mask:       return {0.9f, 0.6f, 0.1f, 1.f};  // orange
    case PcgDataType::Float:      return {0.9f, 0.9f, 0.2f, 1.f};  // yellow
    case PcgDataType::PointCloud: return {0.2f, 0.8f, 0.9f, 1.f};  // cyan
    case PcgDataType::BiomeMap:   return {0.7f, 0.2f, 0.9f, 1.f};  // purple
    case PcgDataType::Vec3:       return {0.9f, 0.4f, 0.4f, 1.f};  // red
    case PcgDataType::Curve:      return {0.5f, 0.5f, 0.9f, 1.f};  // lavender
    default:                      return {0.4f, 0.4f, 0.4f, 1.f};  // grey
    }
}

} // namespace md
