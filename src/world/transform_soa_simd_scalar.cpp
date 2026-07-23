#include <monkey_dust/world/transform_soa_simd.h>

// Compiled with NO special ISA flags — the reference/fallback implementation.
// See transform_soa_simd.h for why this is a separate TU from the AVX2 variant.
namespace md::simd {

void ComputeDistSqScalar(const float* px, const float* pz,
                          float cam_x, float cam_z,
                          float* out_dist_sq, int count) {
    for (int i = 0; i < count; ++i) {
        float dx = px[i] - cam_x, dz = pz[i] - cam_z;
        out_dist_sq[i] = dx * dx + dz * dz;
    }
}

}  // namespace md::simd
