#include <monkey_dust/world/terrain_patch_grid.h>
#include <cstdio>

namespace {
// Number of samples per axis used to estimate each patch's local height
// range at Init() time (kReliefSampleGrid^2 total, e.g. 5x5=25) -- cheap
// enough to run for every patch in the grid once at startup (worst case
// kMaxPatchesPublic=16384 patches * 25 samples = 409600 height-field
// lookups, a few ms one-time cost), and dense enough to catch a real
// ridge/cliff crossing a 300m patch without needing the FULL heightfield
// resolution (that level of precision doesn't matter here -- this value
// only biases WHICH LOD tier gets picked, not the rendered geometry
// itself).
constexpr int kReliefSampleGrid = 5;

// Dilate-then-blur height_range[nz*nx] in place (real bug found live,
// 2026-08-11): a single high-relief patch sitting next to flat neighbours
// could bias its own LOD by up to ~1.4 tiers while its immediate neighbour
// got zero bias -- a raw tier delta the existing skirt/geomorph system was
// never built to hide. That system's own doc comment (UpdateGraniteTerrain,
// scene_render.cpp) records that the OLD distance-only LOD basically never
// produced more than a tiny fractional delta between neighbours (a prior
// neighbor-tier cascade-snap was removed after an A/B test found it fixed
// nothing, because multi-tier neighbour jumps didn't happen); the relief
// bias broke that invariant by construction. Confirmed live via user-
// reported screenshots: a flickering vertical sliver of sky visible
// through the terrain exactly where a high-relief patch bordered a
// low-relief one.
//
// A single max-filter dilation pass does NOT fix this -- it only relocates
// the hard edge one patch further out (the patch just outside the dilation
// radius still sees the FULL undiluted drop to 0). The real fix needs an
// actual smoothing ramp, not just a wider plateau: dilate first (so the
// peak value at/near real relief is never reduced -- the whole point of
// the bias), THEN apply several box-blur passes on top, which spread that
// plateau into a gradual multi-patch ramp with no single-boundary cliff.
// This is the exact "dilate then blur" technique this session's own
// docs/research/TERRAIN_PERF_DEEPSEEK_RESEARCH.md (topic 1, independently,
// for an unrelated VT-paging seam problem) names for the same class of
// problem: a value that must not be under-estimated at its source but
// must transition smoothly.
void DilateAndBlurHeightRange(float* height_range, int nx, int nz) {
    static float filt_a[TerrainPatchGrid::kMaxPatchesPublic];
    static float filt_b[TerrainPatchGrid::kMaxPatchesPublic];
    for (int i = 0; i < nx * nz; ++i) filt_a[i] = height_range[i];

    // Pass 1: 3x3 max-dilate (preserve peak).
    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            float m = filt_a[(size_t)iz * nx + ix];
            for (int dz = -1; dz <= 1; ++dz) {
                int nz2 = iz + dz;
                if (nz2 < 0 || nz2 >= nz) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx2 = ix + dx;
                    if (nx2 < 0 || nx2 >= nx) continue;
                    float v = filt_a[(size_t)nz2 * nx + nx2];
                    if (v > m) m = v;
                }
            }
            filt_b[(size_t)iz * nx + ix] = m;
        }
    }

    // Passes 2-4: repeated 3x3 box-average blur (turns the dilated
    // plateau's edge into a smooth ramp spanning several patches -- each
    // iteration roughly doubles the ramp's width).
    float* src = filt_b;
    float* dst = filt_a;
    constexpr int kBlurIterations = 3;
    for (int iter = 0; iter < kBlurIterations; ++iter) {
        for (int iz = 0; iz < nz; ++iz) {
            for (int ix = 0; ix < nx; ++ix) {
                float sum = 0.f;
                int cnt = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    int nz2 = iz + dz;
                    if (nz2 < 0 || nz2 >= nz) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx2 = ix + dx;
                        if (nx2 < 0 || nx2 >= nx) continue;
                        sum += src[(size_t)nz2 * nx + nx2];
                        ++cnt;
                    }
                }
                dst[(size_t)iz * nx + ix] = sum / (float)cnt;
            }
        }
        float* tmp = src; src = dst; dst = tmp;
    }
    for (int i = 0; i < nx * nz; ++i) height_range[i] = src[i];
}
}  // namespace

void TerrainPatchGrid::Init(float world_origin_x, float world_origin_z, float world_extent,
                             float patch_size, int max_lod, HeightSampleFn height_sampler) {
    world_origin_x_ = world_origin_x;
    world_origin_z_ = world_origin_z;
    patch_size_     = patch_size;
    max_lod_        = max_lod;
    nx_ = (int)std::ceil(world_extent / patch_size);
    nz_ = nx_;
    if ((size_t)nx_ * (size_t)nz_ > (size_t)kMaxPatches) {
        fprintf(stderr, "[TerrainPatchGrid] %dx%d patches exceeds cap %d -- clamping patch count\n",
                nx_, nz_, kMaxPatches);
        // Clamp to a square that fits -- caller should really pick a
        // larger patch_size instead of relying on this, but never
        // overrun the fixed lod_ array.
        int side = (int)std::sqrt((double)kMaxPatches);
        nx_ = nz_ = side;
    }
    for (int i = 0; i < nx_ * nz_; ++i) lod_[i] = 0.f;

    // Precompute each patch's local height range -- see HeightSampleFn's
    // doc comment (terrain_patch_grid.h) for why this is optional/nullable
    // and UpdateLOD's own doc comment for why it matters: distance-only LOD
    // selection coarsens a high-relief patch (e.g. a 500m cliff inside a
    // 460m real Kenshi zone) at the exact same distance as a flat one,
    // which is the confirmed primary cause of "melted"/rounded-looking
    // mountains (docs/research/TERRAIN_GEOMETRY_DEEPSEEK_RESEARCH.md,
    // topics 1/5/7 -- CDLOD/Cesium-quantized-mesh both keep a per-node
    // min/max height specifically for this).
    for (int i = 0; i < nx_ * nz_; ++i) height_range_[i] = 0.f;
    if (height_sampler != nullptr) {
        for (int iz = 0; iz < nz_; ++iz) {
            float oz = world_origin_z_ + (float)iz * patch_size_;
            for (int ix = 0; ix < nx_; ++ix) {
                float ox = world_origin_x_ + (float)ix * patch_size_;
                float hmin = 1e30f, hmax = -1e30f;
                for (int sz = 0; sz < kReliefSampleGrid; ++sz) {
                    float wz = oz + (float)sz / (float)(kReliefSampleGrid - 1) * patch_size_;
                    for (int sx = 0; sx < kReliefSampleGrid; ++sx) {
                        float wx = ox + (float)sx / (float)(kReliefSampleGrid - 1) * patch_size_;
                        float h = height_sampler(wx, wz);
                        if (h < hmin) hmin = h;
                        if (h > hmax) hmax = h;
                    }
                }
                height_range_[(size_t)iz * nx_ + ix] = hmax - hmin;
            }
        }
        DilateAndBlurHeightRange(height_range_, nx_, nz_);
    }
}

void TerrainPatchGrid::UpdateLOD(const float cam_pos[3]) {
    for (int iz = 0; iz < nz_; ++iz) {
        float cz = world_origin_z_ + ((float)iz + 0.5f) * patch_size_;
        for (int ix = 0; ix < nx_; ++ix) {
            float cx = world_origin_x_ + ((float)ix + 0.5f) * patch_size_;
            float dx = cam_pos[0] - cx, dz = cam_pos[2] - cz;
            float dist_sq = dx * dx + dz * dz;
            // log2(dist / patch_size) -- distance normalized by patch_size
            // so tier thresholds scale with this grid's own patch size
            // (tier t starts at roughly patch_size*2^t): without the
            // patch_size_ divisor, dist_sq alone saturates max_lod by
            // ~64m regardless of patch size, putting nearly the entire
            // visible world at the coarsest 1-quad/edge mesh. Equivalent
            // to 0.5*log2(dist_sq / patch_size^2).
            float norm_dist_sq = dist_sq / (patch_size_ * patch_size_);
            float lod = (norm_dist_sq > 1e-3f) ? 0.5f * log2f(norm_dist_sq) : 0.f;

            // Relief bias (docs/research/TERRAIN_GEOMETRY_DEEPSEEK_RESEARCH.md,
            // topic 7's "Concrete change to your current system"): a patch
            // whose height range is large relative to its own footprint
            // (a cliff/ridge, not a rolling hill) needs to stay at a finer
            // LOD than pure distance would give it, because the geometric
            // error of collapsing that relief to a coarser tier is much
            // bigger than for a flat patch at the same distance. Zero when
            // height_range_ is 0 (flat patch, or Init() was called with no
            // height_sampler) -- exactly reproduces the old distance-only
            // formula in both cases, so this cannot regress a patch that
            // was already correctly handled.
            float hr = height_range_[(size_t)iz * nx_ + ix];
            if (hr > 0.f) {
                // log2(1 + range/patch_size) keeps the bias bounded and
                // slowly-growing even for extreme real Kenshi relief
                // (~500m inside a 460m chunk gives log2(1+1.09)=1.06,
                // i.e. a bit over one full LOD tier finer) -- same
                // log2-of-ratio shape the distance term above already
                // uses, so the two compose predictably instead of one
                // dominating unboundedly.
                // TEMPORARILY DISABLED (2026-08-11): live user report of a
                // flickering vertical sky-through-terrain crack near
                // canyon/mesa terrain after this bias shipped, persisting
                // even after the dilate-then-blur smoothing above -- the
                // smoothing reduces but does not fully explain/fix it, so
                // the root cause isn't fully understood yet (task #389).
                // Set to 0 to exactly reproduce the pre-existing distance-
                // only LOD formula (see this block's own comment above:
                // hr>0 is the only way this branch fires, and 0*anything=0
                // either way) while keeping the height_range_/dilate/blur
                // machinery intact for when this is re-investigated.
                constexpr float kReliefLodBias = 0.0f;
                lod -= kReliefLodBias * log2f(1.f + hr / patch_size_);
            }

            if (lod < 0.f) lod = 0.f;
            if (lod > (float)max_lod_) lod = (float)max_lod_;
            lod_[(size_t)iz * nx_ + ix] = lod;
        }
    }
}

namespace {
bool AabbInFrustum(float ox, float oz, float size, float ymin, float ymax, const float fp[16]) {
    for (int p = 0; p < 4; ++p) {
        const float* pl = fp + p * 4;
        float px = (pl[0] >= 0.f) ? (ox + size) : ox;
        float pz = (pl[2] >= 0.f) ? (oz + size) : oz;
        float py = (pl[1] >= 0.f) ? ymax : ymin;
        if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.f) return false;
    }
    return true;
}
} // namespace

int TerrainPatchGrid::SelectVisible(const float frustum_planes[16], VisiblePatch* out, int max_out,
                                     float max_lod_cull) const {
    int count = 0;
    for (int iz = 0; iz < nz_ && count < max_out; ++iz) {
        float oz = world_origin_z_ + (float)iz * patch_size_;
        for (int ix = 0; ix < nx_ && count < max_out; ++ix) {
            float lod = lod_[(size_t)iz * nx_ + ix];
            if (lod > max_lod_cull) continue;
            float ox = world_origin_x_ + (float)ix * patch_size_;
            if (!AabbInFrustum(ox, oz, patch_size_, kAabbMinY, kAabbMaxY, frustum_planes)) continue;
            out[count].ix = ix; out[count].iz = iz;
            out[count].origin_x = ox; out[count].origin_z = oz;
            out[count].lod = lod;
            ++count;
        }
    }
    return count;
}
