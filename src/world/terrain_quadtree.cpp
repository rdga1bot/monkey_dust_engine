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

float QuadtreeSampleHeightRange(TerrainQuadtree::HeightSampleFn sampler, float ox, float oz, float size,
                                 float* out_center_y = nullptr) {
    if (!sampler) {
        if (out_center_y) *out_center_y = 0.f;
        return kQuadtreeSkirtMarginM;
    }
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
    if (out_center_y) *out_center_y = (hmin + hmax) * 0.5f;
    return (hmax - hmin) + kQuadtreeSkirtMarginM;
}

// 2026-08-28 (docs/OPENMW_TERRAIN_BORROWED_TECHNIQUES.md Phase 1): integer
// log2, matching OpenMW's own components/terrain/quadtreeworld.cpp::Log2
// (same signature/semantics -- floor(log2(n)), Log2(0)=0) so the ported
// comparison below reproduces their bucket math exactly, not an
// approximation of it.
unsigned int QuadtreeLog2(unsigned int n) {
    unsigned int lod = 0;
    while (n >>= 1) ++lod;
    return lod;
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
                  TerrainQuadtree::VisibleNode* out, int max_out, int& count,
                  int lod_mode, float min_node_size, float error_to_range_scale) {
    if (count >= max_out) return;

    float cx = ox + size * 0.5f, cz = oz + size * 0.5f;

    // RENDER_VS_ULRICH_CHUNKLOD_DEEPSEEK_RESEARCH.md, section 1 ("LOD
    // selection criteria") + terrain_structure verdict 2026-09-04: this
    // node's own relief was already computed for skirt_depth, but only
    // AFTER the subdivide decision (leaves only) -- moved earlier so the
    // split decision itself can use it as a cheap per-node error bound
    // (the report's own fallback recommendation when no offline per-level
    // error table exists). Reused below for skirt_depth on leaves -- no
    // second sample.
    float center_y = 0.f;
    float height_range = QuadtreeSampleHeightRange(sampler, ox, oz, size, &center_y);

    // Real bug fix (RENDER_VS_ULRICH_CHUNKLOD_DEEPSEEK_RESEARCH.md,
    // "our dist is explicitly 2D... cam_pos[1] is not used"): a camera
    // hovering above a node and a camera at ground level at the same
    // horizontal distance got identical LOD even though the projected
    // screen-space error is very different -- most visible looking down
    // at a steep slope from above. center_y (from the same height sample
    // above) is a cheap per-node Y proxy; true per-vertex Y isn't
    // available at traversal time without a second full sampler pass.
    float dx = cam_pos[0] - cx, dy = cam_pos[1] - center_y, dz = cam_pos[2] - cz;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

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
    //
    // RENDER_VS_ULRICH_CHUNKLOD_DEEPSEEK_RESEARCH.md section 1 + terrain
    // structure verdict 2026-09-04: size*detail_multiplier alone has no
    // terrain-error input at all -- flat ground and a cliff of the same
    // node size got the identical range, which is why steep real Kenshi
    // slopes rendered at the same coarse density as flat ground nearby
    // (the faceted/low-poly look on steep walls, math-confirmed:
    // uniform 3.6m XZ spacing on an 80deg slope is a ~20m vertical rise
    // per triangle). error_to_range_scale converts this node's measured
    // relief (height_range, already sampled above) into a pixel-error-
    // budget-derived range, the same screen-space-error principle
    // Ulrich's real chunked-LOD threshold uses (its own doc comment has
    // the full derivation) -- computed once in SelectVisible from
    // screen_width_px_/fovy_degrees_/kMaxPixelError, not re-derived here.
    // take the max against the existing size-based range so this can
    // only ever ADD subdivision on high-relief nodes, never regress
    // already-tuned flat-terrain behaviour (flat nodes keep height_range
    // near the kQuadtreeSkirtMarginM floor, so range_error stays small
    // and size*detail_multiplier keeps winning there, unchanged).
    float range_size  = size * detail_multiplier;
    float range_error = height_range * error_to_range_scale;
    float range = (range_error > range_size) ? range_error : range_size;

    // lod_mode==1: OpenMW-style discrete comparison (docs/OPENMW_TERRAIN_
    // BORROWED_TECHNIQUES.md Phase 1) -- subdivide when this node's native
    // LOD level (log2(size/minSize), i.e. how many halvings from the
    // finest leaf) is coarser than what the distance "affords"
    // (log2(dist/(minSize*detail_multiplier))). detail_multiplier reused
    // as OpenMW's `factor`; their `cellSize` term collapses to 1 here since
    // sizes are already real world metres, not abstract cell units. This
    // mode has NO morph term at all (matches the ported reference exactly,
    // not an enhancement of it) -- see this function's own doc comment.
    bool shouldSubdivide;
    if (lod_mode == 1) {
        unsigned int nativeLod = (min_node_size > 0.f)
            ? QuadtreeLog2(static_cast<unsigned int>(size / min_node_size + 0.5f))
            : 0;
        float denom = min_node_size * detail_multiplier;
        unsigned int targetLod = (denom > 0.f)
            ? QuadtreeLog2(static_cast<unsigned int>(std::max(0.f, dist / denom)))
            : 0;
        shouldSubdivide = nativeLod > targetLod;
    } else {
        shouldSubdivide = dist < range;
    }
    if (depth < max_depth && shouldSubdivide) {
        float half = size * 0.5f;
        RecurseNode(ox,        oz,        half, depth + 1, max_depth, detail_multiplier, cam_pos, frustum_planes, sampler, out, max_out, count, lod_mode, min_node_size, error_to_range_scale);
        RecurseNode(ox + half, oz,        half, depth + 1, max_depth, detail_multiplier, cam_pos, frustum_planes, sampler, out, max_out, count, lod_mode, min_node_size, error_to_range_scale);
        RecurseNode(ox,        oz + half, half, depth + 1, max_depth, detail_multiplier, cam_pos, frustum_planes, sampler, out, max_out, count, lod_mode, min_node_size, error_to_range_scale);
        RecurseNode(ox + half, oz + half, half, depth + 1, max_depth, detail_multiplier, cam_pos, frustum_planes, sampler, out, max_out, count, lod_mode, min_node_size, error_to_range_scale);
        return;
    }

    // height_range/center_y already sampled above (needed for the split
    // decision itself now) -- reused here for skirt depth and the frustum
    // Y bound, no second sampler pass.
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
    if (lod_mode == 1) morph = 0.f; // OpenMW-style: discrete LOD, no morph term

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
                               TerrainQuadtree::VisibleNode* out, int count, int lod_mode) {
    static QuadtreeNBEntry s_entries[kQuadtreeNBHashSize];
    for (int i = 0; i < kQuadtreeNBHashSize; ++i) s_entries[i].depth = -1;

    for (int i = 0; i < count; ++i) {
        float size = out[i].size;
        int32_t ix = (int32_t)std::lround((out[i].origin_x - world_origin_x) / size);
        int32_t iz = (int32_t)std::lround((out[i].origin_z - world_origin_z) / size);
        QuadtreeNBInsert(s_entries, kQuadtreeNBHashSize, out[i].depth, ix, iz, i);
    }

    // kDirs order: 0=west(-x) 1=east(+x) 2=south(-z) 3=north(+z) -- matches
    // the shader's own north=+z convention (terrain_quadtree.vert's skirt
    // normal assignment). Phase 2's stitch_edge_mask bit order is
    // DIFFERENT (0=north 1=south 2=east 3=west, matching
    // BuildTerrainQuadtreeStitchedIndices) -- kStitchBit below maps d -> that bit.
    constexpr float kDirs[4][2] = { {-1.f,0.f}, {1.f,0.f}, {0.f,-1.f}, {0.f,1.f} };
    constexpr uint8_t kStitchBit[4] = { 0x8, 0x4, 0x2, 0x1 }; // west,east,south,north
    for (int i = 0; i < count; ++i) {
        out[i].use_stitched_mesh = (lod_mode == 1);
    }

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
            if (d_found < 0) continue; // no neighbor at all (world edge) -- plain border, no stitch needed

            if (lod_mode == 1) {
                if (d_found == depth - 1) {
                    out[i].stitch_edge_mask |= kStitchBit[d]; // exactly 1 level coarser -- zipper this edge
                } else if (d_found <= depth - 2) {
                    out[i].needs_skirt_fallback = true; // >=2 level gap -- stitched IBO can't represent this
                }
            }

            if (d_found > depth - 2) continue; // no gap, or gap < 2 levels -- skirt widening moot below

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
    // the reference config. audit S2-10 (2026-08-27): camera.fovy is
    // hardcoded to 45.0f for the whole game (game_init.cpp, never
    // reassigned anywhere in game/src/) and the shipped target resolution
    // is exactly 1280px -- so effective_multiplier == detail_multiplier_
    // bit-for-bit on EVERY real session today, not just at some reference
    // edge case. This is currently forward-looking infrastructure for a
    // future resolution-agnostic build (window resize, settings menu
    // resolution picker), NOT an active LOD mitigation -- do not credit it
    // with any measured FPS/node-count effect for the current target
    // config, and do not re-investigate it as a candidate cause for an FPS
    // complaint reproduced at 1280x720/45fovy (it cannot have changed
    // anything there by construction).
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.f;
    float ref_tan = std::tan(kReferenceFovyDeg * 0.5f * kDeg2Rad);
    float cur_tan = std::tan(fovy_degrees_ * 0.5f * kDeg2Rad);
    float effective_multiplier = detail_multiplier_
        * (screen_width_px_ / kReferenceScreenWidthPx)
        * (ref_tan / cur_tan);

    // RENDER_VS_ULRICH_CHUNKLOD_DEEPSEEK_RESEARCH.md section 1's fallback
    // (no offline per-level error table -> use measured node relief as a
    // conservative per-node error bound). Ulrich's real screen-space-error
    // formula (threshold = error * screen_width / (2*max_pixel_error*
    // tan(fovy/2))) assumes `error` is a SMALL per-vertex simplification
    // residual (his own base_max_error is 1-5m, see docs/
    // TERRAIN_CHUNKLOD_PORT_PLAN.md's spike results) -- plugging our
    // `height_range` directly into that formula in literally applied it
    // and measured live: TerrainPrepCPU/Cull jumped from a typical ~2-3ms
    // to ~17ms at a real steep test spot, because height_range is a WHOLE-
    // NODE relief span (tens to hundreds of metres on real Kenshi cliffs),
    // orders of magnitude bigger than a per-vertex error -- with any
    // physically-sane max_pixel_error (1-5px) the resulting range_error
    // dwarfed camera render distance everywhere, forcing near-universal
    // max-depth subdivision even on gentle terrain, not just real cliffs.
    // kReliefToRangeScale is instead solved directly from the two
    // quantities that matter: at the finest tier (size~57.6m,
    // size*detail_multiplier~172.8m), "typical" gentle-terrain relief
    // (~10m) should give range_error roughly comparable to the existing
    // size-based range (so ordinary terrain is left close to unchanged),
    // while real cliff-scale relief (100-400m, port-plan's own South Hive
    // measurement) drives range_error far past it. 172.8/10 ~= 17.3.
    // Empirically-tuned starting point, not a literal pixel-error budget
    // -- re-measure node_count/Cull-time before trusting a different value.
    constexpr float kReliefToRangeScale = 17.3f;
    float error_to_range_scale = kReliefToRangeScale;

    int num_zones = (int)(world_extent_ / chunk_size_ + 0.5f);
    float min_node_size = chunk_size_ / (float)(1 << max_depth_);
    for (int zz = 0; zz < num_zones && count < max_out; ++zz) {
        for (int zx = 0; zx < num_zones && count < max_out; ++zx) {
            float ox = world_origin_x_ + (float)zx * chunk_size_;
            float oz = world_origin_z_ + (float)zz * chunk_size_;
            RecurseNode(ox, oz, chunk_size_, 0, max_depth_, effective_multiplier,
                        cam_pos, frustum_planes, height_sampler_, out, max_out, count,
                        lod_mode_, min_node_size, error_to_range_scale);
        }
    }
    QuadtreeBalanceNeighbors(world_origin_x_, world_origin_z_, chunk_size_, out, count, lod_mode_);
    return count;
}
