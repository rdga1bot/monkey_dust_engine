#include <monkey_dust/world/terrain_patch_grid.h>
#include <cstdio>
#include <cmath>

namespace {
// Same rationale as the old grid: dense enough to catch a ridge/cliff
// crossing a patch without needing full heightfield resolution -- this
// only biases which tier gets picked, not the rendered geometry.
constexpr int kReliefSampleGrid = 5;
} // namespace

void TerrainPatchGrid::Init(float world_origin_x, float world_origin_z, float world_extent,
                             float patch_size, int max_tier, HeightSampleFn height_sampler) {
    world_origin_x_ = world_origin_x;
    world_origin_z_ = world_origin_z;
    patch_size_     = patch_size;
    max_tier_       = max_tier;
    nx_ = (int)std::ceil(world_extent / patch_size);
    nz_ = nx_;
    if ((size_t)nx_ * (size_t)nz_ > (size_t)kMaxPatches) {
        fprintf(stderr, "[TerrainPatchGrid] %dx%d patches exceeds cap %d -- clamping patch count\n",
                nx_, nz_, kMaxPatches);
        int side = (int)std::sqrt((double)kMaxPatches);
        nx_ = nz_ = side;
    }
    for (int i = 0; i < nx_ * nz_; ++i) lod_[i] = 0.f;
    for (int i = 0; i < nx_ * nz_; ++i) { height_range_[i] = 0.f; height_min_[i] = 0.f; }

    has_height_data_ = (height_sampler != nullptr);
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
                height_min_[(size_t)iz * nx_ + ix]   = hmin;
            }
        }
    }
}

void TerrainPatchGrid::UpdateLOD(const float cam_pos[3]) {
    for (int iz = 0; iz < nz_; ++iz) {
        float cz = world_origin_z_ + ((float)iz + 0.5f) * patch_size_;
        for (int ix = 0; ix < nx_; ++ix) {
            float cx = world_origin_x_ + ((float)ix + 0.5f) * patch_size_;
            float dx = cam_pos[0] - cx, dz = cam_pos[2] - cz;
            float dist_sq = dx * dx + dz * dz;
            float norm_dist_sq = dist_sq / (patch_size_ * patch_size_);
            float lod = (norm_dist_sq > 1e-3f) ? 0.5f * log2f(norm_dist_sq) : 0.f;

            float hr = height_range_[(size_t)iz * nx_ + ix];
            if (hr > 0.f) {
                constexpr float kReliefLodBias = 1.0f;
                lod -= kReliefLodBias * log2f(1.f + hr / patch_size_);
            }

            if (lod < 0.f) lod = 0.f;
            if (lod > (float)max_tier_) lod = (float)max_tier_;
            lod_[(size_t)iz * nx_ + ix] = lod;
            int t = (int)lod;
            if (t < 0) t = 0;
            if (t > max_tier_) t = max_tier_;
            tier_[(size_t)iz * nx_ + ix] = t;
        }
    }

    // Neighbor-tier cascade -- see Tier()'s doc comment (header). Kept
    // for shading/normal continuity, NOT relied on for crack-avoidance
    // (SkirtDepth handles that independently, Phase 0's decision).
    for (int pass = 0; pass <= max_tier_; ++pass) {
        bool changed = false;
        for (int iz = 0; iz < nz_; ++iz) {
            for (int ix = 0; ix < nx_; ++ix) {
                size_t idx = (size_t)iz * nx_ + ix;
                int min_neighbor = max_tier_;
                if (ix > 0)      { int n = tier_[idx - 1];       if (n < min_neighbor) min_neighbor = n; }
                if (ix < nx_-1)  { int n = tier_[idx + 1];       if (n < min_neighbor) min_neighbor = n; }
                if (iz > 0)      { int n = tier_[idx - (size_t)nx_]; if (n < min_neighbor) min_neighbor = n; }
                if (iz < nz_-1)  { int n = tier_[idx + (size_t)nx_]; if (n < min_neighbor) min_neighbor = n; }
                if (tier_[idx] > min_neighbor + 1) {
                    tier_[idx] = min_neighbor + 1;
                    changed = true;
                }
            }
        }
        if (!changed) break;
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
            float ymin = kAabbMinY, ymax = kAabbMaxY;
            if (has_height_data_) {
                float hmin = height_min_[(size_t)iz * nx_ + ix];
                float hmax = hmin + height_range_[(size_t)iz * nx_ + ix];
                ymin = hmin - kAabbSafetyMarginM;
                ymax = hmax + kAabbSafetyMarginM;
            }
            if (!AabbInFrustum(ox, oz, patch_size_, ymin, ymax, frustum_planes)) continue;
            out[count].ix = ix; out[count].iz = iz;
            out[count].origin_x = ox; out[count].origin_z = oz;
            out[count].lod = lod;
            out[count].tier = tier_[(size_t)iz * nx_ + ix];
            ++count;
        }
    }
    return count;
}
