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
// Phase 3 scope: builds the discrete LOD mesh tiers and draws exactly
// ONE hardcoded patch (no instancing yet, Phase 5; no neighbor-LOD snap
// yet, Phase 4) — smallest possible proof that VTF sampling from the new
// world-wide texture actually works end-to-end in a real draw call.
class TerrainPatchRenderer {
public:
    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    static constexpr int kPatchN = 64;    // finest tier: 64x64 quads
    static constexpr int kNumTiers = 7;   // 64,32,16,8,4,2,1 quads/edge

    // Phase 3: draws one patch at the given world origin/size/lod using
    // tier = round(lod) (clamped to [0,kNumTiers-1]) -- no neighbor snap,
    // no instancing.
    void DrawOne(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                 const float* vp16,
                 float origin_x, float origin_z, float patch_size, float lod,
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
