#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_world_heightmap.h>
#include <monkey_dust/render/terrain_renderer.h>
#include <monkey_dust/world/terrain_quadtree.h>

// Final terrain architecture (2026-08-18, "Ogre-quadtree (geomorph+skirts)",
// serene-pondering-teapot.md) -- draws ONE TerrainQuadtree::VisibleNode per
// DrawNode call. Vertex-buffer-less (layout.count=0, matches the
// already-proven Phase 1 spike technique) but INDEXED: ONE shared index
// buffer pair (filled 16x16 grid + 4 skirt strips, terrain_quadtree_mesh.h)
// built once at Init(), reused for EVERY node at EVERY depth everywhere in
// the world -- only the per-node UBO (origin, texelSize, morph, skirtDepth)
// differs per draw. Output contract identical to every prior terrain
// renderer this project has had -- same shaders/terrain_gbuffer_mini.frag,
// unmodified, same packed-normal RGBA32F target.
class TerrainQuadtreeRenderer {
public:
    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Draws ONE node (filled grid + 4 border skirts) inside an already-open
    // G-buffer render pass. node.morph/skirt_depth come straight from
    // TerrainQuadtree::SelectVisible; hmap supplies the world-wide
    // height+normal textures directly (2026-08-24: #398's TerrainHeightClipmap
    // reverted -- see terrain_quadtree.vert's own doc comment for why).
    void DrawNode(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                  const TerrainWorldHeightmap& hmap, const float* vp16,
                  const TerrainQuadtree::VisibleNode& node,
                  float cam_x, float cam_y, float cam_z);

    // Forward (inline) shading revival -- draws node geometry directly into
    // the caller's already-open MAIN color+depth render pass (not an
    // isolated G-buffer), doing full ground-layer shading inline via
    // shaders/terrain_quadtree_forward.frag (shares terrain_quadtree.vert
    // with DrawNode -- only the fragment stage differs). See that shader's
    // doc comment for why this is the revived Variant B architecture
    // (terrain_research/perf/PROGRESS.md, 2026-08-02).
    bool InitForward(SDL_GPUDevice* dev);
    bool IsForwardReady() const { return forward_ready_; }

    // Binds the forward pipeline + PER-FRAME-CONSTANT fragment resources
    // (ground samplers, zoneGroundLayersTex, sun/fog/world_params/cam
    // uniforms) -- call ONCE before the DrawNodeForward loop each frame,
    // mirroring TerrainRenderer::GetSharedGroundSamplers/
    // ZoneGroundLayersTexture's own "shared across all draws" convention.
    void BeginForward(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                      const TerrainRenderer::SunParams& sun,
                      float cam_x, float cam_y, float cam_z,
                      float world_origin_x, float world_origin_z, float world_to_uv,
                      float fog_far, const float fog_color[3], float fog_near,
                      const TerrainRenderer& ground);

    // Draws ONE node -- pipeline/fragment resources already bound by
    // BeginForward; this only pushes the per-node vertex UBO, binds the
    // (world-wide, same for every node) height/normal vertex samplers, and
    // issues the filled+skirt index draws -- same shape as DrawNode.
    void DrawNodeForward(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                         const TerrainWorldHeightmap& hmap, const float* vp16,
                         const TerrainQuadtree::VisibleNode& node,
                         float cam_x, float cam_y, float cam_z);

private:
    GpuPipeline gbuffer_pipeline_;
    GpuPipeline forward_pipeline_;
    GpuStaticBuffer filled_ibo_;
    GpuStaticBuffer skirt_ibo_;
    uint32_t filled_index_count_ = 0;
    uint32_t skirt_index_count_  = 0;
    bool ready_ = false;
    bool forward_ready_ = false;
};
#endif
