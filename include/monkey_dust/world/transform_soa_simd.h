#pragma once

// Phase 2.2/3 (audit): TransformSoA::BulkComputeDistSq used to inline its
// AVX2/scalar bodies directly behind #ifdef __AVX2__ — only ONE variant was
// ever compiled into a given binary (whichever the ambient TU's compile
// flags selected), so there was no way to test that the two stay in sync,
// and no way to runtime-dispatch between them (Phase 3). Extracted here as
// two ALWAYS-declared, independently-callable functions, each implemented
// in its own .cpp with per-TU compile flags (same pattern already used by
// engine/src/vendor/moc/MaskedOcclusionCulling*.cpp — see engine/CMakeLists.txt's
// MOC_BASE_CPP/MOC_AVX2_CPP section) — both link into monkey_dust_engine
// regardless of the ambient -march/-mavx2 flags, since x86-64 doesn't care
// what ISA extensions the CALLING TU was compiled with, only what the
// CALLED function's own object code contains.
//
// px/pz/out must be alignas(64) (or at minimum 32-byte aligned) buffers of
// at least `count` floats — TransformSoA's own px/pz/dist_sq arrays already
// satisfy this (transform_soa.h: alignas(64)).
namespace md::simd {

void ComputeDistSqScalar(const float* px, const float* pz,
                          float cam_x, float cam_z,
                          float* out_dist_sq, int count);

void ComputeDistSqAVX2(const float* px, const float* pz,
                        float cam_x, float cam_z,
                        float* out_dist_sq, int count);

}  // namespace md::simd
