#include <monkey_dust/world/terrain_patch_grid.h>
#include <cstdio>

void TerrainPatchGrid::Init(float world_origin_x, float world_origin_z, float world_extent,
                             float patch_size, int max_lod) {
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
}

void TerrainPatchGrid::UpdateLOD(const float cam_pos[3]) {
    for (int iz = 0; iz < nz_; ++iz) {
        float cz = world_origin_z_ + ((float)iz + 0.5f) * patch_size_;
        for (int ix = 0; ix < nx_; ++ix) {
            float cx = world_origin_x_ + ((float)ix + 0.5f) * patch_size_;
            float dx = cam_pos[0] - cx, dz = cam_pos[2] - cz;
            float dist_sq = dx * dx + dz * dz;
            // 0.5*log2(dist^2) == log2(dist) without a sqrt.
            float lod = (dist_sq > 1e-3f) ? 0.5f * log2f(dist_sq) : 0.f;
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

int TerrainPatchGrid::SelectVisible(const float frustum_planes[16], VisiblePatch* out, int max_out) const {
    int count = 0;
    for (int iz = 0; iz < nz_ && count < max_out; ++iz) {
        float oz = world_origin_z_ + (float)iz * patch_size_;
        for (int ix = 0; ix < nx_ && count < max_out; ++ix) {
            float ox = world_origin_x_ + (float)ix * patch_size_;
            if (!AabbInFrustum(ox, oz, patch_size_, kAabbMinY, kAabbMaxY, frustum_planes)) continue;
            out[count].ix = ix; out[count].iz = iz;
            out[count].origin_x = ox; out[count].origin_z = oz;
            out[count].lod = lod_[(size_t)iz * nx_ + ix];
            ++count;
        }
    }
    return count;
}
