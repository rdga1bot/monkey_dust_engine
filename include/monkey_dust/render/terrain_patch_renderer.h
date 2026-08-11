#pragma once
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_world_heightmap.h>
#include <monkey_dust/world/terrain_patch_grid.h>
#include <SDL3/SDL_gpu.h>

// Granite-style terrain migration, Phase 3 (plan at /home/rdga1/.claude/
// plans/serene-pondering-teapot.md): draws TerrainPatchGrid's patches
// against TerrainWorldHeightmap's single static texture. A NEW, separate
// class from TerrainQuadtreeRenderer (not a modification of it) — the old
// class must keep working untouched through Phase 6's dual-run A/B
// comparison; this is additive, not a replacement, until Phase 8 removes
// the old system entirely.
//
// Phase 3 built the discrete LOD mesh tiers and single-patch VTF draw.
// Phase 4 (2026-07-28, replaced -- see below) added neighbor-LOD edge
// snapping: each tier's mesh baked an edge-mask vertex attribute and the
// shader snapped edge vertices to align with a coarser neighbor's grid.
//
// Phase 4-replacement (verified against the actual reference this whole
// migration is named after -- Themaister/Granite, renderer/ground.cpp):
// the real Granite does NOT snap individual edge vertices. Its
// GroundPatch::refresh() computes `lods = max(inner_lod, vec4(nx,px,nz,
// pz neighbor lods))` -- i.e. if any neighbor is coarser, the WHOLE
// patch draws one tier coarser too, picking a different (coarser) static
// mesh entirely rather than deforming its own finer mesh's edge
// vertices. Adopted here (CPU-side in SceneRender::UpdateGraniteTerrain,
// same formula) because it's strictly simpler -- no edge-mask vertex
// attribute, no per-vertex shader branch, no aInstNeighborTierN stream --
// at the cost of some patches rendering one tier coarser than their own
// ideal distance-based LOD near a coarser neighbor (the same tradeoff
// the reference implementation accepts).
//
// Phase 5 (this revision) replaces the old one-draw-call-per-patch API
// with real hardware instancing: per-patch data (origin, own LOD, 4
// neighbor tiers) moves out of the per-draw uniform buffer into a
// per-instance VERTEX attribute stream bound at slot=1 (GpuVertexLayout's
// inst_attribs, VERTEXINPUTRATE_INSTANCE) -- this deliberately avoids
// the storage-buffer-based instancing NpcRender uses, since this
// pipeline needs frag_samplers=3 and vert_storage_bufs>0 combined with
// frag_samplers>0 is a hard pipeline-creation failure / documented SDL3
// 3.4.8 silent-corruption bug on this hardware (see plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md, Architecture §5).
// All patches sharing the same LOD tier (mesh) now batch into ONE
// instanced SDL_DrawGPUIndexedPrimitives call instead of one call per
// patch.
class TerrainPatchRenderer {
public:
    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // task #299 (2026-07-27): finest tier was 64 quads/edge -> 300m/64 =
    // 4.6875m/vertex, ~23% COARSER than both our own source heightmap's
    // native density (CHUNK_SIZE/TERRAIN_GRID = 460.8/128 = 3.6m) and real
    // Kenshi's actual in-game resolution (RE-confirmed, re_docs/kenshi/
    // terrain.md: 129 verts/460.8m tile = 3.6m) -- i.e. the render was
    // under-sampling data that's already available at higher density,
    // reading as sharper/more faceted terrain than the source supports.
    // 128 quads/edge -> 300/128 = 2.34m/vertex, finer than Kenshi's own
    // resolution (safety margin, not just parity).
    static constexpr int kPatchN = 128;   // finest tier: 128x128 quads
    static constexpr int kNumTiers = 8;   // 128,64,32,16,8,4,2,1 quads/edge
    static constexpr int kMaxInstancesPerTier = 4096;

    // tier_n_ [t] = quads/edge for tier t (128,64,32,16,8,4,2,1).
    static int TierN(int tier) { return tier >= 0 && tier < kNumTiers ? (kPatchN >> tier) : 0; }

    // One patch's per-instance data (12 bytes: matches inst_stride).
    // `lod` here is a RAW continuous value whose floor() MUST equal the
    // tier bucket this instance is stored under -- see
    // BuildInstanceBatches below, the only supported way to construct
    // these (task #389, 2026-08-11: a hand-duplicated version of this
    // logic in two call sites diverged, and even after fixing one copy,
    // a second bug survived where `lod`'s floor() didn't match the tier
    // bucket -- both classes of bug are why this is now centralized
    // instead of "callers must apply X themselves").
    struct Instance {
        float origin_x, origin_z, lod;
    };

    // Grid coordinates + final tier of one patch BuildInstanceBatches
    // actually placed into an instance batch this frame -- callers use
    // this to drive TerrainVtPageCache::RequestPage(ix, iz, tier) without
    // BuildInstanceBatches needing to know TerrainVtPageCache exists.
    struct PlacedPatch {
        int ix, iz, tier;
    };

    // Pure CPU logic (no GPU calls, no VT dependency) -- given this
    // frame's already frustum-culled + LOD-scored visible patches
    // (TerrainPatchGrid::SelectVisible's output, which already carries
    // each patch's final, neighbor-cascaded tier -- see
    // TerrainPatchGrid::Tier's doc comment for that algorithm and its
    // own task #389 history), assigns each to a per-tier instance batch:
    //  1. tier = p.tier, clamped to [0, kNumTiers-1] defensively (in case
    //     of a max_lod/kNumTiers mismatch between grid and renderer).
    //  2. if p.tier is FINER than floor(p.lod) -- i.e. the grid's cascade
    //     lowered this patch's tier below what raw distance/relief alone
    //     would give it -- the lod value written into the instance is
    //     forced to exactly (float)tier (no fractional part) instead of
    //     the raw p.lod. Found live during task #389 (2026-08-11) as a
    //     SECOND independent bug on top of the missing cascade itself:
    //     the shader uses floor(aInstLod) for heightmap mip selection and
    //     fract(aInstLod) for the geomorph morph fraction, both of which
    //     must agree with the tier bucket's ACTUAL mesh resolution
    //     (u.tier_n) -- sending the original raw (coarser) lod after the
    //     tier got clamped finer fed the shader a mip/morph fraction
    //     computed for the wrong tier.
    // This is the ONLY supported way to construct terrain Instances --
    // game (SceneRender) and the editor (World3D viewport) both call this
    // instead of hand-duplicating this logic, which is exactly how the
    // task #389 bug happened in the first place (one copy got fixed, the
    // other didn't, twice).
    //
    // out_insts[t]: caller-owned array of at least max_insts_per_tier
    // Instances, or nullptr to skip tier t entirely. max_insts_per_tier
    // is a caller-supplied capacity, NOT assumed to be kMaxInstancesPerTier
    // -- callers own arrays of DIFFERENT sizes (e.g. game's SceneRender
    // uses 2048/tier, the editor uses 4096/tier), and hardcoding
    // kMaxInstancesPerTier here would silently write past the end of the
    // smaller one.
    // out_counts[kNumTiers]: receives per-tier instance count, capped at
    // max_insts_per_tier (excess patches silently dropped -- same
    // truncation behaviour as before this refactor; callers should size
    // their visible[]/max_out large enough that this never triggers, see
    // TerrainPatchGrid::kMaxPatchesPublic).
    // out_placed/max_placed/out_placed_count (all optional -- pass
    // nullptr/0/nullptr to skip): receives one PlacedPatch per instance
    // actually written (same order as out_insts), up to max_placed.
    static void BuildInstanceBatches(const TerrainPatchGrid::VisiblePatch* visible, int nvis,
                                      Instance* out_insts[kNumTiers], int out_counts[kNumTiers],
                                      int max_insts_per_tier,
                                      PlacedPatch* out_placed = nullptr, int max_placed = 0,
                                      int* out_placed_count = nullptr);

    // Uploads this frame's per-tier instance batches. MUST be called
    // OUTSIDE any active render pass (issues one SDL_GPU copy pass per
    // non-empty tier) and BEFORE the render pass that calls DrawBatchGBuffer/
    // DrawBatchDepthOnly. counts[t]==0 (or insts[t]==nullptr) skips tier t
    // entirely -- those become a no-op for that tier this frame.
    void UploadInstances(SDL_GPUCommandBuffer* cmd,
                          const Instance* const insts[kNumTiers],
                          const int counts[kNumTiers]);

    // task terrain-perf-rebuild Phase 0 (2026-08-01): read-only access to
    // this frame's per-tier instance/index counts for baseline metrics
    // (patch count, vertex count) -- no rendering side effect, safe to call
    // any time after UploadInstances.
    int InstanceCount(int tier) const { return tier >= 0 && tier < kNumTiers ? (int)inst_count_[tier] : 0; }
    uint32_t TierIndexCount(int tier) const { return tier >= 0 && tier < kNumTiers ? tier_idx_count_[tier] : 0; }

    // TERRAIN_CA_REBUILD_PROMPT.md Phase 2 §3 -- depth-only early-Z prepass
    // draw, same instance batch as DrawBatch but with prepass_pipeline_
    // (terrain_patch_prepass.vert + shadow_csm.frag, no color output). Must
    // run inside a depth-only GpuRenderPass (GpuRenderPass::BeginDepthOnly),
    // BEFORE the main color pass that draws this same tier with DrawBatch --
    // see NpcRender::CullAndPrepass's Early-Z block for the established
    // pattern (this mirrors the NPC prepass exactly, just for terrain).
    void DrawBatchDepthOnly(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int tier,
                             const float* vp16, float patch_size,
                             const TerrainWorldHeightmap& hmap,
                             float cam_x, float cam_y, float cam_z);

    // TERRAIN_CA_REBUILD_PROMPT.md Phase 4 -- Variant A (screen-space
    // decoupled shading, see terrain_research/perf/PHASE4_PLAN.md), now the
    // ONLY terrain shading path in this codebase (see
    // terrain_research/ARCHITECTURE_AUDIT_2026_08_04.md -- the forward/
    // inline Variant B this docstring used to compare against, DrawBatch,
    // was removed entirely). Draws the same instance batch with
    // gbuffer_pipeline_ (its own dedicated terrain_patch_gbuffer.vert +
    // terrain_gbuffer_mini.frag), writing world-pos/packed-normal to a
    // caller-provided render target
    // instead of doing any ground-layer shading. Must run inside its own
    // render pass with a DEDICATED depth target (NOT the shared NPC/scene
    // depth -- see TerrainShadingScreenspace's doc comment for why),
    // BEFORE the screen-space shading-resolve pass that reads this data
    // back.
    void DrawBatchGBuffer(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int tier,
                          const float* vp16, float patch_size,
                          const TerrainWorldHeightmap& hmap,
                          float cam_x, float cam_y, float cam_z);

private:
    bool BuildTierMesh(int tier, int quads_per_edge);

    GpuPipeline     prepass_pipeline_;
    GpuPipeline     gbuffer_pipeline_;
    GpuStaticBuffer tier_vbo_[kNumTiers];
    GpuStaticBuffer tier_ibo_[kNumTiers];
    uint32_t        tier_idx_count_[kNumTiers] = {};

    GpuVertexBuffer inst_vbo_[kNumTiers];
    uint32_t        inst_count_[kNumTiers] = {};

    bool ready_ = false;
};
#endif
