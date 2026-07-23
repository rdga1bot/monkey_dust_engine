#include <monkey_dust/world/transform_soa_simd.h>
#include <immintrin.h>

// Compiled with -mavx2 -mfma (or /arch:AVX2 on MSVC) via
// set_source_files_properties in engine/CMakeLists.txt — same per-TU flag
// override pattern as engine/src/vendor/moc/MaskedOcclusionCullingAVX2.cpp.
// Requires px/pz/out_dist_sq to be 32-byte aligned (TransformSoA's own
// arrays are alignas(64) — see transform_soa.h).
namespace md::simd {

void ComputeDistSqAVX2(const float* px, const float* pz,
                        float cam_x, float cam_z,
                        float* out_dist_sq, int count) {
    __m256 cx8 = _mm256_set1_ps(cam_x);
    __m256 cz8 = _mm256_set1_ps(cam_z);
    int n8 = count & ~7;
    for (int i = 0; i < n8; i += 8) {
        __m256 dx = _mm256_sub_ps(_mm256_load_ps(px + i), cx8);
        __m256 dz = _mm256_sub_ps(_mm256_load_ps(pz + i), cz8);
        _mm256_store_ps(out_dist_sq + i, _mm256_fmadd_ps(dx, dx, _mm256_mul_ps(dz, dz)));
    }
    for (int i = n8; i < count; ++i) {
        float dx = px[i] - cam_x, dz = pz[i] - cam_z;
        out_dist_sq[i] = dx * dx + dz * dz;
    }
}

}  // namespace md::simd
