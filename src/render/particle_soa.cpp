#include <monkey_dust/render/particle_soa.h>
#include <cstring>

ParticleSoA::ParticleSoA() {
    memset(life, 0, sizeof(life));
    active_count = 0;
}

float ParticleSoA::RandF(float lo, float hi) {
    rng_ = rng_ * 1664525u + 1013904223u;
    float t = (float)(rng_ >> 8) / (float)(1 << 24);
    return lo + t * (hi - lo);
}

void ParticleSoA::Emit(float ox, float oy, float oz,
                        float vx_range, float vy_range, float vz_range,
                        uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                        float lifetime, float sz, int count, ParticleType /*type*/)
{
    for (int i = 0; i < count && active_count < MAX_PARTICLES; ++i) {
        int s = active_count++;
        px[s] = ox; py[s] = oy; pz[s] = oz;
        vx[s] = RandF(-vx_range, vx_range);
        vy[s] = RandF(0.5f, vy_range);
        vz[s] = RandF(-vz_range, vz_range);
        life[s] = lifetime;
        size[s] = sz;
        r[s] = cr; g[s] = cg; b[s] = cb; a[s] = ca;
    }
}

void ParticleSoA::Update(float dt) {
#ifdef __SSE2__
    __m128 dt4 = _mm_set1_ps(dt);
    __m128 zero4 = _mm_setzero_ps();
    int n4 = active_count & ~3;
    for (int i = 0; i < n4; i += 4) {
        _mm_store_ps(px + i, _mm_add_ps(_mm_load_ps(px + i),
                                         _mm_mul_ps(_mm_load_ps(vx + i), dt4)));
        _mm_store_ps(py + i, _mm_add_ps(_mm_load_ps(py + i),
                                         _mm_mul_ps(_mm_load_ps(vy + i), dt4)));
        _mm_store_ps(pz + i, _mm_add_ps(_mm_load_ps(pz + i),
                                         _mm_mul_ps(_mm_load_ps(vz + i), dt4)));
        _mm_store_ps(life + i, _mm_sub_ps(_mm_load_ps(life + i), dt4));
        (void)zero4;
    }
    for (int i = n4; i < active_count; ++i) {
        px[i] += vx[i] * dt;
        py[i] += vy[i] * dt;
        pz[i] += vz[i] * dt;
        life[i] -= dt;
    }
#else
    for (int i = 0; i < active_count; ++i) {
        px[i] += vx[i] * dt;
        py[i] += vy[i] * dt;
        pz[i] += vz[i] * dt;
        life[i] -= dt;
    }
#endif

    // compact dead particles (swap-with-last)
    for (int i = active_count - 1; i >= 0; --i) {
        if (life[i] <= 0.0f) {
            int last = --active_count;
            if (i != last) {
                px[i] = px[last]; py[i] = py[last]; pz[i] = pz[last];
                vx[i] = vx[last]; vy[i] = vy[last]; vz[i] = vz[last];
                life[i] = life[last]; size[i] = size[last];
                r[i] = r[last]; g[i] = g[last]; b[i] = b[last]; a[i] = a[last];
            }
        }
    }
}

int ParticleSoA::BuildVertices(ParticleVertex* out, int max_out) const {
    int count = 0;
    for (int i = 0; i < active_count && count < max_out; ++i) {
        if (life[i] <= 0.0f) continue;
        ParticleVertex& v = out[count++];
        v.x = px[i]; v.y = py[i]; v.z = pz[i];
        v.size = size[i];
        v.r = r[i]; v.g = g[i]; v.b = b[i]; v.a = a[i];
    }
    return count;
}
