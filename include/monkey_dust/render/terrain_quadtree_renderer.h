#pragma once
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_renderer.h>
#include <SDL3/SDL_gpu.h>
#include <cstdint>

// Quadtree-LOD terrain rewrite, Phase 1-3 (see plan at
// /home/rdga1/.claude/plans/serene-pondering-teapot.md). Renders the nodes
// TerrainQuadtree::SelectVisible picks with real GPU vertex-texture-fetch
// (VTF) height displacement AND continuous CDLOD-style geomorphing (Phase
// 3) — no per-node CPU-baked morph targets (unlike the old TNKN=9 chunk
// system's discrete per-LOD-tier bake, terrain_gen.cpp): each vertex
// computes its own "coarser parent grid" target height in the shader from
// grid parity, since the same shared unit-patch mesh is reused at every
// depth/position with no per-instance CPU data beyond a handful of UBO
// scalars.
//
// Unit-patch mesh: a single NxN-quad grid in LOCAL [0,1]^2 space (float2
// per vertex, no world position baked in — that's the whole CDLOD trick,
// see plan point 2) shared by every instance drawn.
//
// Heightmap: ONE texture covers the WHOLE traversed region (all of
// TerrainQuadtree's root extent), sampled by every node via its own
// origin/size mapped into that texture's UV space — not one texture per
// node (Phase 1's simplification, wrong once more than one node is ever
// drawn at once: neighbouring nodes must sample the SAME underlying height
// data at their shared border, or seams are guaranteed regardless of any
// morphing). GpuTexture (gpu_hal.h) only supports RGBA8 upload today — no
// R16/R32F GPU format exists yet in this HAL. Rather than add one now, the
// uint16 height is packed into the R (low byte) + G (high byte) channels
// of an RGBA8 texture; vsMain reconstructs h16 = r*255 + g*255*256. Safe
// under bilinear filtering because the reconstruction is linear in r and g
// (no intra-byte carry during interpolation).
class TerrainQuadtreeRenderer {
public:
    // Builds the pipeline + unit-patch mesh, then samples TerrainAtlas over
    // the zone rectangle [zx0,zx0+zone_span) x [zy0,zy0+zone_span) at
    // native resolution (zone_span*64+1 samples/axis) and uploads ONE
    // packed heightmap texture covering that whole region. Every node
    // later passed to Draw() must lie within this region — this defines
    // the same bounds the caller's TerrainQuadtree::Init() world_size_m
    // must cover. Returns the real [min,max] height range found so the
    // caller can pass matching values to Draw() for decoding.
    bool Init(int zx0, int zy0, int zone_span, float& out_height_min, float& out_height_max);
    void Shutdown();
    bool IsReady() const { return ready_; }

    float RegionOriginX() const { return region_origin_x_; }
    float RegionOriginZ() const { return region_origin_z_; }
    float RegionSize()    const { return region_size_; }

    // Draws one node from TerrainQuadtree::SelectVisible's output.
    // morph_t (already computed by the caller, see TerrainQuadtreeRenderer
    // ::ComputeMorphT) is this node's CDLOD morph factor in [0,1]: 0 = full
    // own-depth detail, 1 = vertices fully morphed toward the shape this
    // node's PARENT (one depth shallower) would show at this location —
    // matching that shape exactly at t=1 is what makes swapping to the
    // parent (when the camera moves far enough that this node's own
    // recursion threshold is crossed) produce no visible pop.
    //
    // Real Kenshi ground-texture shading (Phase 7 — see terrain_quadtree
    // .slang's fsMain doc comment): reuses terrain_forward.slang's
    // "zone-lookup" fsMain branch (the same one the editor's whole-world
    // synthesis view already uses) rather than duplicating it — a quadtree
    // node, like that branch's synthesis mesh, spans a variable, multi-zone
    // area per draw call with no single "current chunk" to have baked a
    // fixed ground_layers_a/b for. `ground` supplies the 3 already-loaded
    // shading textures (colour overlay, ground DDS array, grass/dirt/road
    // mask — see TerrainRenderer::GetSharedGroundSamplers) and the per-zone
    // lookup SSBO, BORROWED from the caller's existing TerrainRenderer
    // instance — this class never loads its own copy of the ~1GB ground
    // texture set. world_origin_x/z + world_to_uv map this node's session-
    // local world position to the colour atlas's UV space, same convention
    // as TerrainRenderer::DrawRaw's own world_origin_x/z/world_to_uv
    // parameters (callers should pass the identical values).
    void Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
              const float* vp16,
              float origin_x, float origin_z, float size_m,
              float morph_t,
              float height_min_m, float height_max_m,
              const TerrainRenderer::SunParams& sun,
              float cam_x, float cam_y, float cam_z,
              float world_origin_x, float world_origin_z, float world_to_uv,
              float fog_far, const float fog_color[3], float fog_near,
              const TerrainRenderer& ground);

    // t = saturate((dist - lod_distances[depth]) / (parent_threshold - lod_distances[depth])),
    // where parent_threshold = lod_distances[depth-1] (or a value larger
    // than lod_distances[0] for the root, which has no parent to morph
    // toward — root morph_t is always 0). dist = camera-to-node-centre
    // distance, same metric SelectVisible used to pick this node.
    static float ComputeMorphT(float dist, int depth, const float* lod_distances);

private:
    bool BuildUnitPatchMesh();
    bool UploadHeightmapRegion(int zx0, int zy0, int zone_span,
                               float& out_height_min, float& out_height_max);

    static constexpr int kPatchN = 64; // 64x64 quads -> 65x65 verts, uint32 IBO

    GpuPipeline     pipeline_;
    GpuStaticBuffer patch_vbo_;
    GpuStaticBuffer patch_ibo_;
    uint32_t        patch_idx_count_ = 0;

    SDL_GPUTexture* height_tex_     = nullptr;
    SDL_GPUSampler* height_sampler_ = nullptr;

    float region_origin_x_ = 0.f;
    float region_origin_z_ = 0.f;
    float region_size_     = 0.f;
    // World-space distance between adjacent height-texture texels — needed
    // by vsMain's central-difference normal reconstruction (a fixed step in
    // world metres, not a fixed UV fraction, so the normal stays correct
    // regardless of region_span). Computed once in UploadHeightmapRegion.
    float region_texel_step_m_ = 1.f;

    bool ready_ = false;
};
#endif
