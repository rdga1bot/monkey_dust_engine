#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_world_heightmap.h>

// Full-variant Phase 5 (serene-pondering-teapot.md): production
// vertex-buffer-less ("Terra without a vertex buffer") patch renderer.
// ONE pipeline (shaders/terrain_patch_novbo.vert, layout.count=0 --
// zero vertex/index buffers bound, ever) draws EVERY tier: quadsPerEdge
// is a per-draw UBO field, not baked into a static mesh, so there is no
// per-tier mesh to manage (unlike the old, deleted TerrainPatchRenderer,
// which had 8 separate static meshes).
//
// Output contract IDENTICAL to TerrainClipmapRenderer::DrawLevel and the
// old system before it -- same shaders/terrain_gbuffer_mini.frag,
// unmodified, same packed-normal RGBA32F target. TerrainShadingProjected
// needs zero changes.
//
// Callers (game SceneRender / editor World3D) own TerrainPatchGrid --
// this class does not select which patches are visible, only draws the
// ones the caller already picked (same separation TerrainClipmapRenderer
// has from TerrainClipmapCache: cache/grid answers "what", renderer
// answers "how to draw it").
class TerrainPatchRenderer {
public:
    // Coarsest tier's quadsPerEdge is kBaseQuadsPerEdge >> tier (halving
    // per tier, floor at 1) -- matches TerrainClipmapCache::kMeshQuads
    // (128) at tier 0 for a direct density comparison against the
    // existing indexed system, same convention terrain_patch_novbo_spike.vert's
    // own doc comment already established.
    static constexpr int kBaseQuadsPerEdge = 128;
    static int QuadsPerEdgeForTier(int tier) {
        int q = kBaseQuadsPerEdge >> tier;
        return q < 1 ? 1 : q;
    }

    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Draws ONE patch (surface + 4 border skirts, all vertex-buffer-less)
    // inside an already-open G-buffer render pass. tier selects
    // quadsPerEdge via QuadsPerEdgeForTier(); skirt_depth_m should come
    // from TerrainPatchGrid::SkirtDepth(ix,iz) for this patch.
    void DrawPatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                    const TerrainWorldHeightmap& hmap, const float* vp16,
                    float origin_x, float origin_z, int tier, float skirt_depth_m,
                    float cam_x, float cam_y, float cam_z);

private:
    GpuPipeline gbuffer_pipeline_;
    bool ready_ = false;
};
#endif
