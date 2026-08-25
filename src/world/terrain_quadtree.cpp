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

// 2026-08-24: a FIXED skirt margin cannot be correct -- disproved live,
// twice. First guess (5m) left real gaps everywhere; second guess (100m,
// picked from a single South Hive A/B) still left a hole right next to
// the player at a second reported location (11115.5,9676.2), because
// probing md.terrain_height on a grid there found real local relief up
// to ~188m (84m..272m) inside one node footprint -- ALREADY bigger than
// the 100m guess. Real Kenshi terrain relief is NOT bounded by a single
// constant anywhere in the world; the margin has to be measured per node,
// not guessed once and reused everywhere. See QuadtreeSampleHeightRange
// below (this is the same relief sample this code had before, restored --
// it was wrongly judged "vestigial" and deleted on the mistaken assumption
// that CDLOD morph alone already closes the LOD-boundary gap; it doesn't,
// on real steep terrain).
constexpr int kQuadtreeReliefSampleGrid = 5;
constexpr float kQuadtreeSkirtMarginM = 5.0f; // pad on top of measured relief

float QuadtreeSampleHeightRange(TerrainQuadtree::HeightSampleFn sampler, float ox, float oz, float size) {
    if (!sampler) return kQuadtreeSkirtMarginM;
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

    // Real bug fix (30-40 FPS regression investigation): QuadtreeAabbInFrustum
    // below only tests 4 planes (left/right/top/bottom -- see its own doc
    // comment and MdCamera::FrustumPlanes, which explicitly never extracts
    // near/far). With no far-plane and no distance reject, a coarse node far
    // beyond the camera's actual far clip (MdCamera::ProjMatrix hardcodes
    // 4000.f) could still pass the 4-plane test at grazing view angles and
    // get drawn -- confirmed live: depth=0 leaf count spiked from ~10-45 to
    // 2000+ during a 360-degree camera sweep. The GPU's own projection
    // already clips anything past 4000m into nothing, so this reject changes
    // zero pixels on screen -- it only stops wastefully submitting geometry
    // the GPU would discard anyway. `+ size` margin covers this node's own
    // footprint (conservative vs. exact corner distance, consistent with
    // the existing center-distance approximation used for the subdivide
    // decision below).
    constexpr float kMaxRenderDistance = 4000.f; // matches MdCamera::ProjMatrix's far clip
    if (dist > kMaxRenderDistance + size) return;

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

    // height_range doubles as this node's skirt depth (below) and a
    // tighter Y bound for the frustum test than the old fixed fallback --
    // this sampler call was already unavoidable, no added cost from reusing it.
    float height_range = QuadtreeSampleHeightRange(sampler, ox, oz, size);
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
        // 2026-08-24, "falls through textures" user report: skirt_depth
        // MUST scale with this node's own measured relief (height_range) --
        // see QuadtreeSampleHeightRange's doc comment for why a fixed
        // constant was tried twice and disproved twice. On a steep real
        // Kenshi cliff this legitimately produces a large skirt ("curtain")
        // -- that is the correct, complete fix (no visible gap, ever) at
        // the cost of a stretched-texture wall being visible instead of a
        // hole. A cosmetic follow-up (shading the skirt as fog/cliff-color
        // instead of stretched ground texture, or a smarter neighbor-aware
        // sizing) is a SEPARATE task from closing the hole.
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
