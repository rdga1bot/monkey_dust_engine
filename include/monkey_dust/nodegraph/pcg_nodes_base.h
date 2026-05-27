#pragma once
#include "pcg_node.h"
#include <cmath>
#include <cstring>

// FastNoiseLite — already available as a single header in engine/src/vendor/ or third_party/
// We forward-declare usage via the C API to avoid pulling the full header here.
// Concrete implementations call FastNoiseLite directly.

namespace md {

// ── Helper: bilinear sample of a PCG_FIELD_RES×PCG_FIELD_RES float grid ─────
inline float PcgBilinear(const float* grid, float nx, float nz) {
    // nx, nz in [0..1]
    float fx = nx * (PCG_FIELD_RES - 1);
    float fz = nz * (PCG_FIELD_RES - 1);
    int   x0 = (int)fx, z0 = (int)fz;
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x0 >= PCG_FIELD_RES - 1) x0 = PCG_FIELD_RES - 2;
    if (z0 >= PCG_FIELD_RES - 1) z0 = PCG_FIELD_RES - 2;
    float tx = fx - x0, tz = fz - z0;
    float v00 = grid[z0 * PCG_FIELD_RES + x0];
    float v10 = grid[z0 * PCG_FIELD_RES + x0 + 1];
    float v01 = grid[(z0+1) * PCG_FIELD_RES + x0];
    float v11 = grid[(z0+1) * PCG_FIELD_RES + x0 + 1];
    return v00*(1-tx)*(1-tz) + v10*tx*(1-tz) + v01*(1-tx)*tz + v11*tx*tz;
}

// ── LCG random (used in scatter/biome nodes) ──────────────────────────────────
inline float LcgRandF(unsigned& s) {
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) / (float)(1u << 24);
}

// ─────────────────────────────────────────────────────────────────────────────
// PcgNoiseNode — FBM noise via value noise (no external dep for base build)
// P-NG-4 Field mode: returns FieldFn instead of materialising the array.
// ─────────────────────────────────────────────────────────────────────────────
struct PcgNoiseNode : PcgNode {
    float base_scale  = 0.008f;
    float amplitude   = 40.f;
    int   octaves     = 6;
    float persistence = 0.5f;
    float lacunarity  = 2.0f;
    int   seed        = 42;
    bool  field_mode  = true;  // P-NG-4: lazy by default

    const char* TypeName() const override { return "Noise FBM"; }
    PcgDataType OutType()  const override { return PcgDataType::HeightMap; }
    bool UseFieldMode()    const override { return field_mode; }

    PcgData Execute(const PcgData* /*inputs*/, int) override {
        PcgData out = cached; // arena pointer already set by PcgGraph
        if (field_mode) {
            // P-NG-4 path: return lazy FieldFn (ctx = this)
            FieldFn fn;
            fn.ctx      = this;
            fn.evaluate = [](void* ctx, float x, float z) -> float {
                auto* n = static_cast<PcgNoiseNode*>(ctx);
                return n->EvalAt(x, z);
            };
            out.type  = PcgDataType::HeightMap;
            out.field = fn;
            out.count = 0;
            ++out.version;
            return out;
        }

        // Eager path: fill height_ptr array
        for (int zi = 0; zi < PCG_FIELD_RES; ++zi)
            for (int xi = 0; xi < PCG_FIELD_RES; ++xi) {
                float wx = xi * (1.f / PCG_FIELD_RES) / base_scale;
                float wz = zi * (1.f / PCG_FIELD_RES) / base_scale;
                out.height_ptr[zi * PCG_FIELD_RES + xi] = EvalAt(wx, wz);
            }
        ++out.version;
        return out;
    }

    float EvalAt(float x, float z) const {
        // Simple value-noise FBM (no external dep)
        float val = 0.f, amp = 1.f, freq = 1.f, max_amp = 0.f;
        unsigned s = (unsigned)seed;
        float cx = x * base_scale, cz = z * base_scale;
        for (int o = 0; o < octaves; ++o) {
            val    += ValueNoise(cx * freq + o * 31.7f, cz * freq + o * 17.3f, s + o) * amp;
            max_amp += amp;
            amp    *= persistence;
            freq   *= lacunarity;
        }
        return (val / max_amp) * amplitude;
    }

private:
    static float ValueNoise(float x, float z, unsigned seed_off) {
        int   ix = (int)floorf(x), iz = (int)floorf(z);
        float fx = x - ix,         fz = z - iz;
        // Smoothstep
        fx = fx * fx * (3.f - 2.f * fx);
        fz = fz * fz * (3.f - 2.f * fz);
        auto h = [&](int gx, int gz) {
            unsigned s = (unsigned)(gx * 1619 + gz * 31337 + seed_off * 6271);
            s = s * 1664525u + 1013904223u;
            return (float)(s >> 8) / (float)(1u << 24);
        };
        return h(ix,iz)*(1-fx)*(1-fz) + h(ix+1,iz)*fx*(1-fz)
             + h(ix,iz+1)*(1-fx)*fz   + h(ix+1,iz+1)*fx*fz;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgAddNode — adds two HeightMap inputs; lazy-capable (P-NG-4)
// ─────────────────────────────────────────────────────────────────────────────
struct PcgAddNode : PcgNode {
    float   weight_a = 1.f, weight_b = 1.f;
    FieldFn upstream_a_ = {};   // stored per-Execute for lazy composition
    FieldFn upstream_b_ = {};

    const char* TypeName()  const override { return "Add"; }
    PcgDataType OutType()   const override { return PcgDataType::HeightMap; }
    PcgDataType InType(int) const override { return PcgDataType::HeightMap; }
    bool UseFieldMode()     const override { return true; }

    PcgData Execute(const PcgData inputs[], int in_count) override {
        PcgData out = cached;
        // count==0 sentinel: lazy upstream (eager nodes set count=PCG_FIELD_RES²)
        bool a_lazy = in_count > 0 && inputs[0].type == PcgDataType::HeightMap && inputs[0].count == 0;
        bool b_lazy = in_count > 1 && inputs[1].type == PcgDataType::HeightMap && inputs[1].count == 0;
        bool a_none = in_count <= 0 || inputs[0].type == PcgDataType::None;
        bool b_none = in_count <= 1 || inputs[1].type == PcgDataType::None;
        if ((a_lazy || a_none) && (b_lazy || b_none)) {
            upstream_a_ = a_lazy ? inputs[0].field : FieldFn::Null();
            upstream_b_ = b_lazy ? inputs[1].field : FieldFn::Null();
            FieldFn fn;
            fn.ctx = this;
            fn.evaluate = [](void* ctx, float x, float z) -> float {
                auto* n = static_cast<PcgAddNode*>(ctx);
                float va = n->upstream_a_.valid() ? n->upstream_a_.evaluate(n->upstream_a_.ctx, x, z) : 0.f;
                float vb = n->upstream_b_.valid() ? n->upstream_b_.evaluate(n->upstream_b_.ctx, x, z) : 0.f;
                return va * n->weight_a + vb * n->weight_b;
            };
            out.type  = PcgDataType::HeightMap;
            out.field = fn;
            out.count = 0;
            ++out.version;
            return out;
        }
        // Eager fallback: at least one input has a materialized buffer
        const float* a = (in_count > 0 && inputs[0].count > 0) ? inputs[0].height_ptr : nullptr;
        const float* b = (in_count > 1 && inputs[1].count > 0) ? inputs[1].height_ptr : nullptr;
        int N = PCG_FIELD_RES * PCG_FIELD_RES;
        for (int i = 0; i < N; ++i)
            out.height_ptr[i] = (a ? a[i] * weight_a : 0.f) + (b ? b[i] * weight_b : 0.f);
        ++out.version;
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgMultiplyNode — multiplies HeightMap by a Mask
// ─────────────────────────────────────────────────────────────────────────────
struct PcgMultiplyNode : PcgNode {
    const char* TypeName()       const override { return "Multiply"; }
    PcgDataType OutType()        const override { return PcgDataType::HeightMap; }
    PcgDataType InType(int i)    const override {
        return i == 0 ? PcgDataType::HeightMap : PcgDataType::Mask;
    }

    PcgData Execute(const PcgData inputs[], int in_count) override {
        PcgData out = cached;
        const float* h = (in_count > 0 && inputs[0].height_ptr) ? inputs[0].height_ptr : nullptr;
        const float* m = (in_count > 1 && inputs[1].mask_ptr)   ? inputs[1].mask_ptr   : nullptr;
        int N = PCG_FIELD_RES * PCG_FIELD_RES;
        for (int i = 0; i < N; ++i)
            out.height_ptr[i] = (h ? h[i] : 0.f) * (m ? m[i] : 1.f);
        ++out.version;
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgClampNode — clamps HeightMap to [lo, hi]; lazy-capable (P-NG-4)
// ─────────────────────────────────────────────────────────────────────────────
struct PcgClampNode : PcgNode {
    float   lo = 0.f, hi = 100.f;
    FieldFn upstream_ = {};

    const char* TypeName()  const override { return "Clamp"; }
    PcgDataType OutType()   const override { return PcgDataType::HeightMap; }
    PcgDataType InType(int) const override { return PcgDataType::HeightMap; }
    bool UseFieldMode()     const override { return true; }

    PcgData Execute(const PcgData inputs[], int in_count) override {
        PcgData out = cached;
        bool src_lazy = in_count > 0 && inputs[0].type == PcgDataType::HeightMap && inputs[0].count == 0;
        if (src_lazy) {
            upstream_ = inputs[0].field;
            FieldFn fn;
            fn.ctx = this;
            fn.evaluate = [](void* ctx, float x, float z) -> float {
                auto* n = static_cast<PcgClampNode*>(ctx);
                float v = n->upstream_.valid() ? n->upstream_.evaluate(n->upstream_.ctx, x, z) : 0.f;
                return v < n->lo ? n->lo : (v > n->hi ? n->hi : v);
            };
            out.type  = PcgDataType::HeightMap;
            out.field = fn;
            out.count = 0;
            ++out.version;
            return out;
        }
        // Eager fallback
        const float* src = (in_count > 0 && inputs[0].count > 0) ? inputs[0].height_ptr : nullptr;
        int N = PCG_FIELD_RES * PCG_FIELD_RES;
        for (int i = 0; i < N; ++i) {
            float v = src ? src[i] : 0.f;
            out.height_ptr[i] = v < lo ? lo : (v > hi ? hi : v);
        }
        ++out.version;
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgSlopeNode — converts HeightMap to a Mask based on gradient magnitude
// ─────────────────────────────────────────────────────────────────────────────
struct PcgSlopeNode : PcgNode {
    float world_size = 500.f; // metres across PCG_FIELD_RES cells
    float lo = 0.f, hi = 0.5f; // normalised slope [0..1] → mask

    const char* TypeName()    const override { return "Slope"; }
    PcgDataType OutType()     const override { return PcgDataType::Mask; }
    PcgDataType InType(int)   const override { return PcgDataType::HeightMap; }

    PcgData Execute(const PcgData inputs[], int in_count) override {
        PcgData out = cached; // mask_ptr set by arena
        const float* h = (in_count > 0 && inputs[0].height_ptr) ? inputs[0].height_ptr : nullptr;
        float cell = world_size / PCG_FIELD_RES;
        int R = PCG_FIELD_RES;
        for (int z = 0; z < R; ++z) {
            for (int x = 0; x < R; ++x) {
                float c  = h ? h[z*R+x] : 0.f;
                float dx = h ? (h[z*R+((x<R-1)?x+1:x)] - h[z*R+((x>0)?x-1:x)]) / (2*cell) : 0.f;
                float dz = h ? (h[((z<R-1)?z+1:z)*R+x] - h[((z>0)?z-1:z)*R+x]) / (2*cell) : 0.f;
                (void)c;
                float slope = sqrtf(dx*dx + dz*dz);
                // Remap [lo..hi] → [0..1]
                float t = (slope - lo) / (hi - lo + 1e-6f);
                out.mask_ptr[z*R+x] = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
            }
        }
        ++out.version;
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgDomainWarpNode — warps XZ coords by a secondary noise; lazy-capable (P-NG-4)
// ─────────────────────────────────────────────────────────────────────────────
struct PcgDomainWarpNode : PcgNode {
    float   warp_strength  = 30.f;
    float   warp_scale     = 0.003f;
    float   world_size     = 500.f;   // metres; used to convert offsets in lazy mode
    int     warp_seed      = 99;
    FieldFn upstream_field_ = {};

    const char* TypeName()  const override { return "Domain Warp"; }
    PcgDataType OutType()   const override { return PcgDataType::HeightMap; }
    PcgDataType InType(int) const override { return PcgDataType::HeightMap; }
    bool UseFieldMode()     const override { return true; }

    PcgData Execute(const PcgData inputs[], int in_count) override {
        PcgData out = cached;
        bool src_lazy = in_count > 0 && inputs[0].type == PcgDataType::HeightMap && inputs[0].count == 0;
        if (src_lazy) {
            upstream_field_ = inputs[0].field;
            FieldFn fn;
            fn.ctx = this;
            fn.evaluate = [](void* ctx, float x, float z) -> float {
                auto* n = static_cast<PcgDomainWarpNode*>(ctx);
                // Map world-space (x,z) to the same noise coords as the eager path
                float inv_ws = n->world_size > 0.f ? 1.f / n->world_size : 0.f;
                float wx = x * inv_ws * (float)PCG_FIELD_RES * n->warp_scale;
                float wz = z * inv_ws * (float)PCG_FIELD_RES * n->warp_scale;
                float ox_g = PcgDomainWarpNode::WarpNoise(wx + 1.7f, wz + 9.2f, n->warp_seed)     * n->warp_strength;
                float oz_g = PcgDomainWarpNode::WarpNoise(wx + 8.3f, wz + 2.8f, n->warp_seed + 1) * n->warp_strength;
                // Convert grid-cell offsets back to world-space
                float cell = n->world_size / (float)PCG_FIELD_RES;
                return n->upstream_field_.evaluate(n->upstream_field_.ctx,
                                                   x + ox_g * cell, z + oz_g * cell);
            };
            out.type  = PcgDataType::HeightMap;
            out.field = fn;
            out.count = 0;
            ++out.version;
            return out;
        }
        // Eager fallback (bilinear sample of materialized buffer at warped coords)
        const float* src = (in_count > 0 && inputs[0].count > 0) ? inputs[0].height_ptr : nullptr;
        int R = PCG_FIELD_RES;
        for (int zi = 0; zi < R; ++zi) {
            for (int xi = 0; xi < R; ++xi) {
                float wx = xi * warp_scale, wz = zi * warp_scale;
                float ox = WarpNoise(wx + 1.7f, wz + 9.2f, warp_seed)     * warp_strength;
                float oz = WarpNoise(wx + 8.3f, wz + 2.8f, warp_seed + 1) * warp_strength;
                float sx = (xi + ox) / R, sz = (zi + oz) / R;
                sx = sx < 0.f ? 0.f : (sx > 1.f ? 1.f : sx);
                sz = sz < 0.f ? 0.f : (sz > 1.f ? 1.f : sz);
                out.height_ptr[zi * R + xi] = src ? PcgBilinear(src, sx, sz) : 0.f;
            }
        }
        ++out.version;
        return out;
    }

    static float WarpNoise(float x, float z, int s) {
        int ix = (int)floorf(x), iz = (int)floorf(z);
        float fx = x - ix, fz = z - iz;
        fx = fx*fx*(3-2*fx); fz = fz*fz*(3-2*fz);
        auto h = [&](int gx, int gz) {
            unsigned sv = (unsigned)(gx*1619 + gz*31337 + s*6271);
            sv = sv*1664525u+1013904223u;
            return (float)(sv>>8)/(float)(1u<<24)*2.f-1.f;
        };
        return h(ix,iz)*(1-fx)*(1-fz) + h(ix+1,iz)*fx*(1-fz)
             + h(ix,iz+1)*(1-fx)*fz   + h(ix+1,iz+1)*fx*fz;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgRidgeNode — ridge/mountain effect via 1 - |noise|
// ─────────────────────────────────────────────────────────────────────────────
struct PcgRidgeNode : PcgNode {
    float ridge_weight      = 0.8f;  // blend weight [0..1]
    float redistrib_power   = 1.6f;  // pow(ridged, power) — sharpens peaks

    const char* TypeName()    const override { return "Ridge"; }
    PcgDataType OutType()     const override { return PcgDataType::HeightMap; }
    PcgDataType InType(int)   const override { return PcgDataType::HeightMap; }

    PcgData Execute(const PcgData inputs[], int in_count) override {
        PcgData out = cached;
        const float* src = (in_count > 0 && inputs[0].height_ptr) ? inputs[0].height_ptr : nullptr;
        if (!src) { memset(out.height_ptr, 0, PCG_FIELD_RES*PCG_FIELD_RES*sizeof(float)); ++out.version; return out; }
        int N = PCG_FIELD_RES * PCG_FIELD_RES;
        // Find range for normalisation
        float vmin = src[0], vmax = src[0];
        for (int i = 1; i < N; ++i) {
            if (src[i] < vmin) vmin = src[i];
            if (src[i] > vmax) vmax = src[i];
        }
        float range = vmax - vmin + 1e-6f;
        for (int i = 0; i < N; ++i) {
            float n = (src[i] - vmin) / range;       // [0..1]
            float r = 1.f - fabsf(2.f * n - 1.f);   // ridge: 0 at extremes, 1 at centre
            r = powf(r < 0.f ? 0.f : r, redistrib_power);
            out.height_ptr[i] = src[i] * (1.f - ridge_weight) + r * vmax * ridge_weight;
        }
        ++out.version;
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgScatterNode — Poisson-disk scatter of objects using a density Mask
// ─────────────────────────────────────────────────────────────────────────────
struct PcgScatterNode : PcgNode {
    float    density    = 0.02f;   // base points per m²
    float    radius     = 4.f;     // minimum distance between points (m)
    float    world_size = 500.f;
    int      seed       = 1;
    uint16_t prefab_id  = 0;

    const char* TypeName()    const override { return "Scatter"; }
    PcgDataType OutType()     const override { return PcgDataType::PointCloud; }
    PcgDataType InType(int i) const override {
        return i == 0 ? PcgDataType::Mask : PcgDataType::None;
    }

    PcgData Execute(const PcgData inputs[], int in_count) override {
        PcgData out = cached; // scatter_ptr set by arena
        const float* mask = (in_count > 0 && inputs[0].mask_ptr) ? inputs[0].mask_ptr : nullptr;
        int R   = PCG_FIELD_RES;
        float cell = world_size / R;
        float half = world_size * 0.5f;
        float r2  = radius * radius;

        unsigned s = (unsigned)seed;
        int count  = 0;

        for (int z = 0; z < R && count < PCG_MAX_SCATTER; ++z) {
            for (int x = 0; x < R && count < PCG_MAX_SCATTER; ++x) {
                float d = mask ? mask[z * R + x] : 1.f;
                float threshold = density * cell * cell * d;
                if (LcgRandF(s) > threshold) continue;

                // Candidate world position
                float px = x * cell - half + LcgRandF(s) * cell;
                float pz = z * cell - half + LcgRandF(s) * cell;

                // Radius check against recent points (last 32)
                int start = count > 32 ? count - 32 : 0;
                bool ok = true;
                for (int k = start; k < count && ok; ++k) {
                    float ex = out.scatter_ptr[k].x - px;
                    float ez = out.scatter_ptr[k].z - pz;
                    if (ex*ex + ez*ez < r2) ok = false;
                }
                if (!ok) continue;

                out.scatter_ptr[count++] = {
                    px, pz,
                    0.8f + LcgRandF(s) * 0.4f,    // scale
                    LcgRandF(s) * 6.2832f,          // rot_y
                    prefab_id, {}
                };
            }
        }
        out.count = count;
        ++out.version;
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgBiomeNode — Voronoi biome regions → BiomeMap
// ─────────────────────────────────────────────────────────────────────────────
struct PcgBiomeNode : PcgNode {
    float biome_scale  = 0.00025f;  // world-space frequency of biome centres
    int   num_biomes   = 6;
    int   seed         = 77;

    const char* TypeName()    const override { return "Biome"; }
    PcgDataType OutType()     const override { return PcgDataType::BiomeMap; }

    PcgData Execute(const PcgData* /*inputs*/, int) override {
        PcgData out = cached;
        int R = PCG_FIELD_RES;
        for (int z = 0; z < R; ++z)
            for (int x = 0; x < R; ++x)
                out.biome_ptr[z * R + x] = VoronoiBiome(x, z);
        ++out.version;
        return out;
    }

private:
    uint8_t VoronoiBiome(int xi, int zi) const {
        float wx = xi * biome_scale, wz = zi * biome_scale;
        int   cx = (int)floorf(wx), cz = (int)floorf(wz);
        float best_d = 1e9f;
        int   best_b = 0;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                unsigned s = (unsigned)((cx+dx)*1619 + (cz+dz)*31337 + seed*6271);
                s = s*1664525u+1013904223u;
                float jx = cx + dx + (float)(s >> 8) / (float)(1u<<24);
                s = s*1664525u+1013904223u;
                float jz = cz + dz + (float)(s >> 8) / (float)(1u<<24);
                float ex = jx - wx, ez = jz - wz;
                float d  = ex*ex + ez*ez;
                if (d < best_d) {
                    best_d = d;
                    unsigned bid = s*1664525u+1013904223u;
                    best_b = (int)(bid >> 8) % (num_biomes > 0 ? num_biomes : 1);
                }
            }
        }
        return (uint8_t)best_b;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PcgOutputNode — terminal sink; exposes final TerrainGenParams
// ─────────────────────────────────────────────────────────────────────────────
struct PcgOutputNode : PcgNode {
    const float*   final_height  = nullptr;
    const uint8_t* final_biome   = nullptr;

    // Materialisation target for lazy (FieldFn) upstream inputs.
    // OutType()==None so SetupArenaPointers won't assign cached.height_ptr.
    static inline float s_scratch_[PCG_FIELD_RES * PCG_FIELD_RES] = {};

    const char* TypeName()    const override { return "Terrain Output"; }
    PcgDataType OutType()     const override { return PcgDataType::None; }
    PcgDataType InType(int i) const override {
        if (i == 0) return PcgDataType::HeightMap;
        if (i == 1) return PcgDataType::BiomeMap;
        return PcgDataType::None;
    }

    PcgData Execute(const PcgData inputs[], int in_count) override {
        if (in_count > 0 && inputs[0].type == PcgDataType::HeightMap) {
            // count==0 is the sentinel that lazy-path nodes set; eager nodes set count>0.
            // Do NOT call field.valid() on eager data — height_ptr and field share union bytes.
            if (inputs[0].count == 0) {
                if (inputs[0].field.valid())
                    inputs[0].field.Materialize(s_scratch_, PCG_FIELD_RES, PCG_FIELD_RES, 500.f);
                else
                    memset(s_scratch_, 0, sizeof(s_scratch_));
                final_height = s_scratch_;
            } else {
                final_height = inputs[0].height_ptr;
            }
        }
        if (in_count > 1 && inputs[1].type == PcgDataType::BiomeMap)
            final_biome = inputs[1].biome_ptr;

        PcgData out = PcgData::MakeNone();
        ++out.version;
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BiomeSplatNode — emits BiomeSplatUBO parameters for terrain_splat shader (P-NG-6.4)
// InType[0]=HeightMap, InType[1]=BiomeMap. OutType=None (parameter sink only).
// Read splat params from this node when uploading the BiomeSplatUBO to the GPU.
// ─────────────────────────────────────────────────────────────────────────────
struct BiomeSplatNode : PcgNode {
    float biome_color[3][4] = {             // base color per biome (rgba)
        {0.25f, 0.55f, 0.15f, 1.f},        // biome 0: grassland
        {0.65f, 0.52f, 0.35f, 1.f},        // biome 1: desert
        {0.15f, 0.35f, 0.25f, 1.f},        // biome 2: swamp
    };
    float biome_rock_color[3][4] = {
        {0.45f, 0.42f, 0.38f, 1.f},
        {0.58f, 0.48f, 0.32f, 1.f},
        {0.35f, 0.32f, 0.28f, 1.f},
    };
    float snow_height      = 150.f;  // metres above which snow appears
    float rock_slope       = 0.5f;   // dot(N,Up) below which rock shows
    float blend_width      = 15.f;   // metres of transition band
    float triplanar_scale  = 0.05f;  // 1 / tile_size_metres

    const char* TypeName()    const override { return "Biome Splat"; }
    PcgDataType OutType()     const override { return PcgDataType::None; }
    PcgDataType InType(int i) const override {
        if (i == 0) return PcgDataType::HeightMap;
        if (i == 1) return PcgDataType::BiomeMap;
        return PcgDataType::None;
    }

    PcgData Execute(const PcgData* /*inputs*/, int) override {
        PcgData out = PcgData::MakeNone();
        ++out.version;
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BiomeSplatUBO — C++ mirror of terrain_splat.frag BiomeSplatUBO (std140, set=3 b=0).
// Populate via BiomeSplatNode_FillUBO(); upload as a frag UBO before DrawIndexed.
// std140 packing: vec4 → 16B, float → 4B (no padding needed between scalar fields).
// ─────────────────────────────────────────────────────────────────────────────
struct BiomeSplatUBO {
    float biome_color[3][4];       // vec4[3] — base color per biome
    float biome_rock_color[3][4];  // vec4[3] — rock overlay per biome
    float snow_height;
    float rock_slope;
    float blend_width;
    float triplanar_scale;
};  // 112B (3×16 + 3×16 + 4×4)
static_assert(sizeof(BiomeSplatUBO) == 112, "BiomeSplatUBO layout mismatch");

inline void BiomeSplatNode_FillUBO(const BiomeSplatNode& node, BiomeSplatUBO& ubo) noexcept {
    for (int b = 0; b < 3; ++b) {
        for (int c = 0; c < 4; ++c) {
            ubo.biome_color[b][c]      = node.biome_color[b][c];
            ubo.biome_rock_color[b][c] = node.biome_rock_color[b][c];
        }
    }
    ubo.snow_height     = node.snow_height;
    ubo.rock_slope      = node.rock_slope;
    ubo.blend_width     = node.blend_width;
    ubo.triplanar_scale = node.triplanar_scale;
}

} // namespace md
