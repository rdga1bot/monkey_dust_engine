#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_world_heightmap.h>
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
    // TerrainQuadtree::SelectVisible; hmap supplies both the height VTF and
    // the Phase-3 world-wide baked normal texture the shader double-samples
    // for geomorph.
    void DrawNode(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                  const TerrainWorldHeightmap& hmap, const float* vp16,
                  const TerrainQuadtree::VisibleNode& node,
                  float cam_x, float cam_y, float cam_z);

private:
    GpuPipeline gbuffer_pipeline_;
    GpuStaticBuffer filled_ibo_;
    GpuStaticBuffer skirt_ibo_;
    uint32_t filled_index_count_ = 0;
    uint32_t skirt_index_count_  = 0;
    bool ready_ = false;
};
#endif
