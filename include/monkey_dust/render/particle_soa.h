#pragma once
#include <cstdint>

#ifdef __SSE2__
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

static constexpr int MAX_PARTICLES = 8192;

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

    int BuildVertices(ParticleVertex* out, int max_out) const;

private:
    ParticleSoA();
    float RandF(float lo, float hi);
    unsigned int rng_ = 0xDEADBEEFu;
};
