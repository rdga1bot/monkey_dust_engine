#pragma once
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_renderer.h>
#include <monkey_dust/render/terrain_world_heightmap.h>
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
// Phase 4 added neighbor-LOD edge snapping: each tier's mesh bakes an
// edge-mask vertex attribute (which of the 4 patch edges a vertex sits
// on) at build time, and the shader snaps a patch's edge vertices to
// align with a COARSER neighbor's own grid, eliminating T-junction
// cracks at LOD boundaries.
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

    // tier_n_ [t] = quads/edge for tier t (128,64,32,16,8,4,2,1) -- exposed so
    // callers (TerrainPatchGrid-driven selection) can convert a
    // neighbor's LOD float into the tier_n value Instance::neighbor_tier_n
    // expects without duplicating the halving sequence.
    static int TierN(int tier) { return tier >= 0 && tier < kNumTiers ? (kPatchN >> tier) : 0; }

    // One patch's per-instance data (28 bytes: matches inst_stride).
    // neighbor_tier_n order: -X,+X,-Z,+Z. Pass 0 (or this patch's own
    // TierN) for "no coarser neighbor to snap against" (world edge, or a
    // same-or-finer neighbor) -- same convention Phase 4's DrawOne used.
    struct Instance {
        float origin_x, origin_z, lod;
        float neighbor_tier_n[4];
    };

    // Uploads this frame's per-tier instance batches. MUST be called
    // OUTSIDE any active render pass (issues one SDL_GPU copy pass per
    // non-empty tier) and BEFORE the render pass that calls DrawBatch.
    // counts[t]==0 (or insts[t]==nullptr) skips tier t entirely --
    // DrawBatch(t) becomes a no-op for that tier this frame.
    void UploadInstances(SDL_GPUCommandBuffer* cmd,
                          const Instance* const insts[kNumTiers],
                          const int counts[kNumTiers]);

    // Draws every instance uploaded for `tier` this frame as ONE
    // instanced indexed draw call. No-op if UploadInstances wasn't
    // called this frame or uploaded 0 instances for this tier.
    void DrawBatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int tier,
                   const float* vp16, float patch_size,
                   const TerrainWorldHeightmap& hmap,
                   const TerrainRenderer::SunParams& sun,
                   float cam_x, float cam_y, float cam_z,
                   float world_origin_x, float world_origin_z, float world_to_uv,
                   float fog_far, const float fog_color[3], float fog_near,
                   const TerrainRenderer& ground);

private:
    bool BuildTierMesh(int tier, int quads_per_edge);

    GpuPipeline     pipeline_;
    GpuStaticBuffer tier_vbo_[kNumTiers];
    GpuStaticBuffer tier_ibo_[kNumTiers];
    uint32_t        tier_idx_count_[kNumTiers] = {};

    GpuVertexBuffer inst_vbo_[kNumTiers];
    uint32_t        inst_count_[kNumTiers] = {};

    bool ready_ = false;
};
#endif
