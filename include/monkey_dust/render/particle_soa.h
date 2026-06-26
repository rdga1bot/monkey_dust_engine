#pragma once
#include <cstdint>

#ifdef __SSE2__
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

static constexpr int   MAX_PARTICLES               = 8192;
// VBfA-derived particle cull constants (RE 2026-06-25)
static constexpr float PARTICLE_VIS_PADDING         = 100.0f; // start rendering 100u before dist threshold
static constexpr float PARTICLE_VIS_DIST_DEFAULT    = 500.0f; // per-system max visibility distance
static constexpr float PARTICLE_BOUNDING_RADIUS_MULT = 1.5f;  // emitter cull sphere = extent × 1.5

enum class ParticleType : uint8_t { SPARK = 0, SMOKE = 1 };

struct ParticleVertex {
    float   x, y, z, size;
    uint8_t r, g, b, a;
}; // stride = 20

class ParticleSoA {
public:
    static ParticleSoA& Get() {
        static ParticleSoA inst;
        return inst;
    }

    alignas(16) float px[MAX_PARTICLES];
    alignas(16) float py[MAX_PARTICLES];
    alignas(16) float pz[MAX_PARTICLES];
    alignas(16) float vx[MAX_PARTICLES];
    alignas(16) float vy[MAX_PARTICLES];
    alignas(16) float vz[MAX_PARTICLES];
    alignas(16) float life[MAX_PARTICLES];

    float   size[MAX_PARTICLES];
    uint8_t r[MAX_PARTICLES], g[MAX_PARTICLES], b[MAX_PARTICLES], a[MAX_PARTICLES];
    int     active_count = 0;

    void Emit(float ox, float oy, float oz,
              float vx_range, float vy_range, float vz_range,
              uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
              float lifetime, float sz, int count, ParticleType type);

    void Update(float dt);

    // Camera-distance culled build (VBfA-OPT-1).
    // max_dist=0 → no culling. Pass PARTICLE_VIS_DIST_DEFAULT for normal use.
    int BuildVertices(ParticleVertex* out, int max_out,
                      float cam_x = 0.f, float cam_y = 0.f, float cam_z = 0.f,
                      float max_dist = 0.f) const;

    // Emitter-level cull: returns false if emitter sphere is beyond visibility range.
    // extent = half-size of emitter; cull radius = extent * PARTICLE_BOUNDING_RADIUS_MULT.
    static bool ShouldEmit(float ox, float oy, float oz, float extent,
                            float cam_x, float cam_y, float cam_z,
                            float vis_dist_max = PARTICLE_VIS_DIST_DEFAULT);

private:
    ParticleSoA();
    float RandF(float lo, float hi);
    unsigned int rng_ = 0xDEADBEEFu;
};
