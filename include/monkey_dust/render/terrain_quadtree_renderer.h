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
    bool Init(md::GpuDeviceHandle dev);
    void Shutdown(md::GpuDeviceHandle dev);
    bool IsReady() const { return ready_; }

    // Draws ONE node (filled grid + 4 border skirts) inside an already-open
    // G-buffer render pass. node.morph/skirt_depth come straight from
    // TerrainQuadtree::SelectVisible; hmap supplies the world-wide
    // height+normal textures directly (2026-08-24: #398's TerrainHeightClipmap
    // reverted -- see terrain_quadtree.vert's own doc comment for why).
    void DrawNode(SDL_GPURenderPass* rp, md::GpuCommandBufferHandle cmd,
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
    bool InitForward(md::GpuDeviceHandle dev);
    bool IsForwardReady() const { return forward_ready_; }

    // Binds the forward pipeline + PER-FRAME-CONSTANT fragment resources
    // (ground samplers, zoneGroundLayersTex, sun/fog/world_params/cam
    // uniforms) -- call ONCE before the DrawNodeForward loop each frame,
    // mirroring TerrainRenderer::GetSharedGroundSamplers/
    // ZoneGroundLayersTexture's own "shared across all draws" convention.
    void BeginForward(SDL_GPURenderPass* rp, md::GpuCommandBufferHandle cmd,
                      const TerrainRenderer::SunParams& sun,
                      float cam_x, float cam_y, float cam_z,
                      float world_origin_x, float world_origin_z, float world_to_uv,
                      float fog_far, const float fog_color[3], float fog_near,
                      const TerrainRenderer& ground);

    // Draws ONE node -- pipeline/fragment resources already bound by
    // BeginForward; this only pushes the per-node vertex UBO, binds the
    // (world-wide, same for every node) height/normal vertex samplers, and
    // issues the filled+skirt index draws -- same shape as DrawNode.
    void DrawNodeForward(SDL_GPURenderPass* rp, md::GpuCommandBufferHandle cmd,
                         const TerrainWorldHeightmap& hmap, const float* vp16,
                         const TerrainQuadtree::VisibleNode& node,
                         float cam_x, float cam_y, float cam_z);

    // B3 (Granite terrain finding, RENDER_VS_GRANITE_DEEPSEEK_RESEARCH.md):
    // Granite instances up to 512 patches in one draw_indexed; our DrawNode
    // loop issues 2 draws + 1 UBO push PER node instead. This is the
    // instanced replacement for the G-buffer path specifically (DrawNode
    // stays as-is, still used by the forward path and any other caller).
    //
    // Per-node data (origin, texelSize, morph, skirtDepth) is read from a
    // TEXTURE (nodeDataTex, 2 texels/node, gl_InstanceIndex-indexed) in a
    // SEPARATE vertex shader (terrain_quadtree_batched.vert), not an SSBO:
    // no shader anywhere in this codebase combines vert_samplers>0 with
    // vert_storage_bufs>0, and this exact combination sits in the same
    // territory as multiple already-documented Intel Gen9 ANV silent-fail/
    // hang bugs (GpuPipeline::Create's own vert_storage_bufs+frag_samplers
    // guard, gpu_hal_pipeline.cpp). terrain_quadtree.vert already has 2
    // vertex samplers (heightTex, normalTex); a 3rd sampler reuses an
    // already-proven-safe resource category instead of introducing an
    // untested-on-this-hardware one. Kept as a fully separate vertex
    // shader (not a shared/parameterized one) specifically so the forward
    // pipeline (real frag_samplers=5) never risks inheriting any change
    // made here.
    bool InitBatched(md::GpuDeviceHandle dev);
    bool IsBatchedReady() const { return batched_ready_; }

    // Packs `count` nodes into the CPU-side staging buffer and uploads to
    // nodeDataTex. MUST run inside an already-open SDL_GPUCopyPass on the
    // frame's own command buffer, BEFORE the G-buffer render pass opens --
    // mirrors TerrainVtPageCache::UploadPageMeta's "caller owns the copy
    // pass" convention (SDL_GPU disallows nesting a copy pass inside an
    // already-active render pass). Capped at kMaxBatchedNodes.
    void UploadNodeData(md::GpuDeviceHandle dev, SDL_GPUCopyPass* cp,
                        const TerrainQuadtree::VisibleNode* nodes, int count);

    // Binds the batched pipeline + per-frame-constant vertex resources
    // (height/normal/nodeData samplers, vp/height_range/cam_pos UBO).
    void BeginBatched(SDL_GPURenderPass* rp, md::GpuCommandBufferHandle cmd,
                      const TerrainWorldHeightmap& hmap, const float* vp16,
                      float cam_x, float cam_y, float cam_z);

    // Issues ONE instanced draw_indexed for the filled grid + ONE for the
    // skirt strips, covering the `count` nodes uploaded via UploadNodeData
    // this frame (count must match the UploadNodeData call this frame).
    void DrawBatched(SDL_GPURenderPass* rp, md::GpuCommandBufferHandle cmd, int count);

    static constexpr int kMaxBatchedNodes = 8192; // half of kMaxNodesPublic -- generous vs typical per-frame visible counts
    static constexpr int kNodeDataTexWidth = 256;

private:
    GpuPipeline gbuffer_pipeline_;
    GpuPipeline forward_pipeline_;
    GpuPipeline batched_pipeline_;
    GpuStaticBuffer filled_ibo_;
    GpuStaticBuffer skirt_ibo_;
    uint32_t filled_index_count_ = 0;
    uint32_t skirt_index_count_  = 0;

    // docs/OPENMW_TERRAIN_BORROWED_TECHNIQUES.md Phase 2: all 16
    // BuildTerrainQuadtreeStitchedIndices(edgeMask) variants concatenated
    // into ONE GPU buffer at Init() -- stitched_offsets_[mask] is the byte
    // offset into stitched_ibo_ for that mask's slice (SDL_GPUBufferBinding
    // supports a nonzero offset into a shared buffer), stitched_counts_[mask]
    // is its index count. Only consulted in DrawNode when
    // node.use_stitched_mesh && !node.needs_skirt_fallback.
    GpuStaticBuffer stitched_ibo_;
    uint32_t stitched_offsets_[16] = {};
    uint32_t stitched_counts_[16]  = {};
    bool ready_ = false;
    bool forward_ready_ = false;
    bool batched_ready_ = false;
    md::GpuTextureHandle node_data_tex_     = nullptr;
    SDL_GPUSampler* node_data_sampler_ = nullptr;
};
#endif
