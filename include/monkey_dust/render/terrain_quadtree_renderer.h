#pragma once
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_hal.h>
#include <SDL3/SDL_gpu.h>
#include <cstdint>

// Quadtree-LOD terrain rewrite, Phase 1 (see plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md). Renders ONE
// hardcoded unit patch with real GPU vertex-texture-fetch (VTF) height
// displacement — no traversal, no LOD selection, no geomorphing yet
// (Phase 2+). Confirms the vert_samplers>0 + frag_samplers>0 pattern
// (empirically disproven as unsafe on this hardware, 2026-07-25 — see
// CLAUDE.md's "Intel HD 520 — Hardware Checklist") works end-to-end inside
// the real engine and not just the isolated standalone test harness.
//
// Unit-patch mesh: a single NxN-quad grid in LOCAL [0,1]^2 space (float2
// per vertex, no world position baked in — that's the whole CDLOD trick,
// see plan point 2) shared by every instance drawn. Height comes from a
// vertex-stage Sample() against a heightmap texture, not from CPU-baked
// vertex data.
//
// Heightmap encoding: GpuTexture (gpu_hal.h) only supports RGBA8 upload
// today (InitFromMemory) — no R16/R32F GPU format exists yet in this HAL.
// Rather than add one for a Phase-1 proof, the uint16 height is packed into
// the R (low byte) + G (high byte) channels of an RGBA8 texture; vsMain
// reconstructs h16 = r*255 + g*255*256. This is safe under bilinear
// filtering because the reconstruction is linear in r and g (no intra-byte
// carry during interpolation) — filtering the two channels independently
// and reconstructing is equivalent to filtering the reconstructed height
// directly.
class TerrainQuadtreeRenderer {
public:
    // Builds the pipeline + unit-patch mesh, then samples TerrainAtlas zone
    // (zx,zy) over its native 65x65 grid and uploads the packed heightmap
    // texture. Returns the real [min,max] height range found so the caller
    // can pass matching values to Draw() for decoding.
    bool Init(int zx, int zy, float& out_height_min, float& out_height_max);
    void Shutdown();
    bool IsReady() const { return ready_; }

    // Draws the single hardcoded patch. origin_x/origin_z/size_m define its
    // world-space footprint (must match the world region the heightmap
    // texture was sampled from at Init() time — Phase 1 has exactly one
    // fixed patch, no re-sampling per call).
    void Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
              const float* vp16,
              float origin_x, float origin_z, float size_m,
              float height_min_m, float height_max_m);

private:
    bool BuildUnitPatchMesh();
    bool UploadHeightmap(int zx, int zy, float& out_height_min, float& out_height_max);

    static constexpr int kPatchN = 64; // 64x64 quads -> 65x65 verts, uint32 IBO

    GpuPipeline     pipeline_;
    GpuStaticBuffer patch_vbo_;
    GpuStaticBuffer patch_ibo_;
    uint32_t        patch_idx_count_ = 0;

    SDL_GPUTexture* height_tex_     = nullptr;
    SDL_GPUSampler* height_sampler_ = nullptr;

    bool ready_ = false;
};
#endif
