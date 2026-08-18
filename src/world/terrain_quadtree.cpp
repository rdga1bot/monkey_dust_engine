#include <monkey_dust/world/terrain_quadtree.h>
#include <cmath>

namespace {
// Names prefixed Quadtree* to avoid Unity-build anonymous-namespace
// collisions with terrain_patch_grid.cpp's identically-shaped helpers
// (same TU once both land in the same unity batch).
bool QuadtreeAabbInFrustum(float ox, float oz, float size, float ymin, float ymax, const float fp[16]) {
    for (int p = 0; p < 4; ++p) {
        const float* pl = fp + p * 4;
        float px = (pl[0] >= 0.f) ? (ox + size) : ox;
        float pz = (pl[2] >= 0.f) ? (oz + size) : oz;
        float py = (pl[1] >= 0.f) ? ymax : ymin;
        if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.f) return false;
    }
    return true;
}

// Same 5x5 relief-sample convention TerrainPatchGrid used (this session's
// Phase 4) -- cheap enough to run per VISIBLE node per frame (only drawn
// nodes pay this, a small bounded set, not all zones).
constexpr int kQuadtreeReliefSampleGrid = 5;
constexpr float kQuadtreeSkirtMarginM = 5.0f;

float QuadtreeSampleHeightRange(TerrainQuadtree::HeightSampleFn sampler, float ox, float oz, float size) {
    if (!sampler) return 0.f;
    float hmin = 1e30f, hmax = -1e30f;
    for (int sz = 0; sz < kQuadtreeReliefSampleGrid; ++sz) {
        float wz = oz + (float)sz / (float)(kQuadtreeReliefSampleGrid - 1) * size;
        for (int sx = 0; sx < kQuadtreeReliefSampleGrid; ++sx) {
            float wx = ox + (float)sx / (float)(kQuadtreeReliefSampleGrid - 1) * size;
            float h = sampler(wx, wz);
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }
    return (hmax - hmin) + kQuadtreeSkirtMarginM;
}
} // namespace

void TerrainQuadtree::Init(float world_origin_x, float world_origin_z, float world_extent,
                            float chunk_size, int max_depth, float detail_multiplier,
                            HeightSampleFn height_sampler) {
    world_origin_x_ = world_origin_x;
    world_origin_z_ = world_origin_z;
    world_extent_   = world_extent;
    chunk_size_     = chunk_size;
    max_depth_      = max_depth;
    detail_multiplier_ = detail_multiplier;
    height_sampler_ = height_sampler;
}

namespace {
// Recursive per-zone traversal -- no persistent state, purely a function of
// (origin, size, depth) and the camera. See terrain_quadtree.h's own doc
// comment for the CDLOD-style range/morph derivation this implements.
void RecurseNode(float ox, float oz, float size, int depth, int max_depth,
                  float detail_multiplier, const float cam_pos[3], const float frustum_planes[16],
                  TerrainQuadtree::HeightSampleFn sampler,
                  TerrainQuadtree::VisibleNode* out, int max_out, int& count) {
    if (count >= max_out) return;

    float cx = ox + size * 0.5f, cz = oz + size * 0.5f;
    float dx = cam_pos[0] - cx, dz = cam_pos[2] - cz;
    float dist = std::sqrt(dx * dx + dz * dz);

    // range(this node's own size) -- see header doc comment: subdivide if
    // camera is within range of THIS node; if not (or max depth reached),
    // draw this node with a morph blending toward its own parent shape as
    // dist approaches range.
    float range = size * detail_multiplier;

    if (depth < max_depth && dist < range) {
        float half = size * 0.5f;
        RecurseNode(ox,        oz,        half, depth + 1, max_depth, detail_multiplier, cam_pos, frustum_planes, sampler, out, max_out, count);
        RecurseNode(ox + half, oz,        half, depth + 1, max_depth, detail_multiplier, cam_pos, frustum_planes, sampler, out, max_out, count);
        RecurseNode(ox,        oz + half, half, depth + 1, max_depth, detail_multiplier, cam_pos, frustum_planes, sampler, out, max_out, count);
        RecurseNode(ox + half, oz + half, half, depth + 1, max_depth, detail_multiplier, cam_pos, frustum_planes, sampler, out, max_out, count);
        return;
    }

    // Conservative whole-world Y bound for culling -- tightened below via
    // the same relief sample already needed for skirt depth.
    float height_range = QuadtreeSampleHeightRange(sampler, ox, oz, size);
    // HeightRange alone doesn't give an absolute Y bound (no HeightMin
    // tracked here since we don't persist per-node state) -- use a
    // generous fixed fallback band; this only affects culling
    // conservativeness, not correctness (skirt depth is exact).
    constexpr float kFallbackYMin = -500.f, kFallbackYMax = 4000.f;
    if (!QuadtreeAabbInFrustum(ox, oz, size, kFallbackYMin, kFallbackYMax, frustum_planes)) return;

    // Morph must reach 1.0 exactly where THIS node would stop being drawn
    // at all -- i.e. where its OWN PARENT's range boundary sits, not where
    // this node's own (smaller) range sits. A node can legitimately be
    // drawn at any distance up to parentRange (its parent only recursed
    // into this node because dist(parent) < range(parent) = parentRange;
    // this child can sit anywhere within the parent's footprint, so its
    // own dist can approach parentRange even though it's already far
    // beyond its own, much smaller range). Using `range` here instead
    // (this node's own range) put most of a leaf's visible footprint past
    // the "fully morphed" point, i.e. morph=1 for nearly the whole visible
    // leaf area instead of only a thin band near its real LOD boundary --
    // the corrected reference is parentRange = size(parent)*detail_multiplier
    // = (size*2)*detail_multiplier = range*2 (range halves exactly one
    // level per depth by construction, so this is exact, not an estimate).
    float parent_range = range * 2.0f;
    constexpr float kTransitionRatio = 0.3f; // fraction of parent_range used for the morph blend band
    float morph = 1.0f - (parent_range - dist) / (parent_range * kTransitionRatio);
    if (morph < 0.f) morph = 0.f;
    if (morph > 1.f) morph = 1.f;
    if (depth == 0) morph = 0.f; // zone root has no parent to blend toward

    if (count < max_out) {
        out[count].origin_x = ox;
        out[count].origin_z = oz;
        out[count].size = size;
        out[count].depth = depth;
        out[count].morph = morph;
        out[count].skirt_depth = height_range;
        ++count;
    }
}
} // namespace

int TerrainQuadtree::SelectVisible(const float cam_pos[3], const float frustum_planes[16],
                                    VisibleNode* out, int max_out) const {
    int count = 0;
    int num_zones = (int)(world_extent_ / chunk_size_ + 0.5f);
    for (int zz = 0; zz < num_zones && count < max_out; ++zz) {
        for (int zx = 0; zx < num_zones && count < max_out; ++zx) {
            float ox = world_origin_x_ + (float)zx * chunk_size_;
            float oz = world_origin_z_ + (float)zz * chunk_size_;
            RecurseNode(ox, oz, chunk_size_, 0, max_depth_, detail_multiplier_,
                        cam_pos, frustum_planes, height_sampler_, out, max_out, count);
        }
    }
    return count;
}
