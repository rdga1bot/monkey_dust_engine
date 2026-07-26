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
// Phase 4 (this revision) adds neighbor-LOD edge snapping: each tier's
// mesh bakes an edge-mask vertex attribute (which of the 4 patch edges a
// vertex sits on) at build time; DrawOne now takes the 4 neighbor tier
// indices so the shader can snap this patch's edge vertices to align
// with a COARSER neighbor's own grid, eliminating T-junction cracks at
// LOD boundaries. Instancing (Phase 5) still pending.
class TerrainPatchRenderer {
public:
    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    static constexpr int kPatchN = 64;    // finest tier: 64x64 quads
    static constexpr int kNumTiers = 7;   // 64,32,16,8,4,2,1 quads/edge

    // tier_n_ [t] = quads/edge for tier t (64,32,16,8,4,2,1) -- exposed so
    // callers (TerrainPatchGrid-driven selection, Phase 5+) can convert a
    // neighbor's LOD float into the tier_n value DrawOne's neighbor_tier_n
    // params expect without duplicating the halving sequence.
    static int TierN(int tier) { return tier >= 0 && tier < kNumTiers ? (kPatchN >> tier) : 0; }

    // Draws one patch at the given world origin/size/lod using
    // tier = round(lod) (clamped to [0,kNumTiers-1]). neighbor_tier_n
    // (order: -X,+X,-Z,+Z) should be TierN(neighbor's own rounded LOD) --
    // pass 0 (or this patch's own TierN) for "no coarser neighbor to snap
    // against" (world edge, or a same-or-finer neighbor).
    void DrawOne(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                 const float* vp16,
                 float origin_x, float origin_z, float patch_size, float lod,
                 const float neighbor_tier_n[4],
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

    bool ready_ = false;
};
#endif
