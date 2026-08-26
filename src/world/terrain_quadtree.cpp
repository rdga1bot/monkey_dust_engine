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

void TerrainQuadtree::SetScreenParams(float screen_width_px, float fovy_degrees) {
    screen_width_px_ = screen_width_px > 1.f ? screen_width_px : 1280.f;
    fovy_degrees_    = fovy_degrees > 1.f ? fovy_degrees : 45.f;
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

namespace {
constexpr float kReferenceScreenWidthPx = 1280.f; // Intel HD 520 target res (CLAUDE.md)
constexpr float kReferenceFovyDeg       = 45.f;   // game_init.cpp camera.fovy default
} // namespace

namespace {
// B2 (Ulrich activation-level-propagation finding, RENDER_VS_ULRICH_
// CHUNKLOD_DEEPSEEK_RESEARCH.md): lightweight neighbor-LOD-mismatch
// mitigation SUPPLEMENT, not a replacement for skirts -- the report's own
// verdict is explicit that even Ulrich's real reference relies on
// skirts/overlap (EXTRA_BOX_SIZE, "crack-filling skirts"), not a pure
// structural no-gap guarantee.
//
// The plan for this step originally called for INSERTING a new node one
// level finer than a >=2-levels-coarser neighbor to fill the gap
// structurally. Implementing that revealed a real correctness bug before
// it shipped: the coarser ancestor node is already emitted and covers
// that same footprint, so inserting a finer node on top of it (rather
// than replacing/shrinking the ancestor, a much larger structural
// change) would draw OVERLAPPING geometry -- z-fighting, not a fix. This
// project's own terrain-thrashing history (docs/TERRAIN_PATTERNS_SURVEY.md)
// is exactly the pattern of shipping unverified terrain-geometry changes
// on paper-plan confidence, so this got caught and scoped down here
// instead of shipped broken.
//
// What ships instead: for each emitted node with a same-depth neighbor
// slot covered only by an ancestor >=2 levels coarser, widen THIS node's
// own skirt_depth to at least that coarse neighbor's already-computed
// relief (out[].skirt_depth) -- the real vertical discontinuity that
// needs hiding across that edge. Pure data adjustment on already-emitted
// nodes, zero new geometry, zero overlap risk, reuses the skirt
// mechanism already shipped and proven (2026-08-24 "falls through
// textures" fix).
struct QuadtreeNBEntry { int32_t depth, ix, iz, node_index; };
constexpr int kQuadtreeNBHashSize = 32768; // pow2, > 2x kMaxNodesPublic(16384) for load factor

uint32_t QuadtreeNBHash(int32_t depth, int32_t ix, int32_t iz) {
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)depth) * 16777619u;
    h = (h ^ (uint32_t)ix)    * 16777619u;
    h = (h ^ (uint32_t)iz)    * 16777619u;
    return h;
}

// Returns the stored node_index if (depth,ix,iz) is present, else -1.
int QuadtreeNBFind(const QuadtreeNBEntry entries[], int table_size,
                    int32_t depth, int32_t ix, int32_t iz) {
    uint32_t h = QuadtreeNBHash(depth, ix, iz) & (uint32_t)(table_size - 1);
    for (int probe = 0; probe < table_size; ++probe) {
        int slot = (int)((h + (uint32_t)probe) & (uint32_t)(table_size - 1));
        const QuadtreeNBEntry& e = entries[slot];
        if (e.depth == -1) return -1; // empty slot: not found
        if (e.depth == depth && e.ix == ix && e.iz == iz) return e.node_index;
    }
    return -1; // table full and not found (should not happen, sized with margin)
}

void QuadtreeNBInsert(QuadtreeNBEntry entries[], int table_size,
                       int32_t depth, int32_t ix, int32_t iz, int node_index) {
    uint32_t h = QuadtreeNBHash(depth, ix, iz) & (uint32_t)(table_size - 1);
    for (int probe = 0; probe < table_size; ++probe) {
        int slot = (int)((h + (uint32_t)probe) & (uint32_t)(table_size - 1));
        QuadtreeNBEntry& e = entries[slot];
        if (e.depth == -1 || (e.depth == depth && e.ix == ix && e.iz == iz)) {
            e.depth = depth; e.ix = ix; e.iz = iz; e.node_index = node_index;
            return;
        }
    }
    // Table full: drop silently -- sized with a 2x margin above
    // kMaxNodesPublic specifically so this should never trigger.
}

void QuadtreeBalanceNeighbors(float world_origin_x, float world_origin_z, float chunk_size,
                               TerrainQuadtree::VisibleNode* out, int count) {
    static QuadtreeNBEntry s_entries[kQuadtreeNBHashSize];
    for (int i = 0; i < kQuadtreeNBHashSize; ++i) s_entries[i].depth = -1;

    for (int i = 0; i < count; ++i) {
        float size = out[i].size;
        int32_t ix = (int32_t)std::lround((out[i].origin_x - world_origin_x) / size);
        int32_t iz = (int32_t)std::lround((out[i].origin_z - world_origin_z) / size);
        QuadtreeNBInsert(s_entries, kQuadtreeNBHashSize, out[i].depth, ix, iz, i);
    }

    constexpr float kDirs[4][2] = { {-1.f,0.f}, {1.f,0.f}, {0.f,-1.f}, {0.f,1.f} };
    for (int i = 0; i < count; ++i) {
        int depth = out[i].depth;
        float size = out[i].size;
        if (depth < 1) continue; // no ancestor can be >=2 levels coarser than depth 0

        for (int d = 0; d < 4; ++d) {
            float nb_ox = out[i].origin_x + kDirs[d][0] * size;
            float nb_oz = out[i].origin_z + kDirs[d][1] * size;

            int32_t same_ix = (int32_t)std::lround((nb_ox - world_origin_x) / size);
            int32_t same_iz = (int32_t)std::lround((nb_oz - world_origin_z) / size);
            if (QuadtreeNBFind(s_entries, kQuadtreeNBHashSize, depth, same_ix, same_iz) >= 0)
                continue; // same-depth neighbor already exists -- no gap

            int d_found = -1, found_index = -1;
            for (int dd = depth - 1; dd >= 0; --dd) {
                float size_dd = chunk_size / (float)(1 << dd);
                float coarse_ox = std::floor((nb_ox - world_origin_x) / size_dd) * size_dd + world_origin_x;
                float coarse_oz = std::floor((nb_oz - world_origin_z) / size_dd) * size_dd + world_origin_z;
                int32_t cix = (int32_t)std::lround((coarse_ox - world_origin_x) / size_dd);
                int32_t ciz = (int32_t)std::lround((coarse_oz - world_origin_z) / size_dd);
                int idx = QuadtreeNBFind(s_entries, kQuadtreeNBHashSize, dd, cix, ciz);
                if (idx >= 0) { d_found = dd; found_index = idx; break; }
            }
            if (d_found < 0 || d_found > depth - 2) continue; // no gap, or gap < 2 levels

            float coarse_relief = out[found_index].skirt_depth;
            if (coarse_relief > out[i].skirt_depth) out[i].skirt_depth = coarse_relief;
        }
    }
}
} // namespace

int TerrainQuadtree::SelectVisible(const float cam_pos[3], const float frustum_planes[16],
                                    VisibleNode* out, int max_out) const {
    int count = 0;
    // B1: scale detail_multiplier_ by actual screen width / FOV relative to
    // the reference config -- at the reference (SetScreenParams never
    // called, or called with 1280/45) this is an exact 1.0x no-op, so the
    // recursion below is byte-for-byte identical to the pre-B1 formula.
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.f;
    float ref_tan = std::tan(kReferenceFovyDeg * 0.5f * kDeg2Rad);
    float cur_tan = std::tan(fovy_degrees_ * 0.5f * kDeg2Rad);
    float effective_multiplier = detail_multiplier_
        * (screen_width_px_ / kReferenceScreenWidthPx)
        * (ref_tan / cur_tan);
    int num_zones = (int)(world_extent_ / chunk_size_ + 0.5f);
    for (int zz = 0; zz < num_zones && count < max_out; ++zz) {
        for (int zx = 0; zx < num_zones && count < max_out; ++zx) {
            float ox = world_origin_x_ + (float)zx * chunk_size_;
            float oz = world_origin_z_ + (float)zz * chunk_size_;
            RecurseNode(ox, oz, chunk_size_, 0, max_depth_, effective_multiplier,
                        cam_pos, frustum_planes, height_sampler_, out, max_out, count);
        }
    }
    QuadtreeBalanceNeighbors(world_origin_x_, world_origin_z_, chunk_size_, out, count);
    return count;
}
