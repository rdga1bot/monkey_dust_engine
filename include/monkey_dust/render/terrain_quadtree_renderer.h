#pragma once
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/terrain_renderer.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_atomic.h>
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
    // the ABSOLUTE zone rectangle [zx0,zx0+zone_span) x [zy0,zy0+zone_span)
    // at native resolution (zone_span*64+1 samples/axis) and uploads ONE
    // packed heightmap texture covering that whole region.
    //
    // local_origin_x/z: the region's origin EXPRESSED IN WHATEVER
    // COORDINATE SPACE the caller's TerrainQuadtree lives in — this is
    // what gets stored as region_origin_x_/z_ and compared against node
    // origins in the shader's regionUV computation, so it MUST match
    // exactly. Deliberately separate from zx0/zy0 (always absolute Kenshi
    // zone indices — TerrainAtlas itself is absolute-only): a caller whose
    // camera/tree lives in absolute Kenshi metres (e.g. the editor's
    // free-fly viewport) passes local_origin_x = zx0*CHUNK_SIZE (same
    // value, effectively a no-op translation); a caller using a floating
    // SESSION-LOCAL window (e.g. the game's SceneRender::tnoff_x/z, see
    // its own doc comment) must pass THAT origin instead — passing the
    // absolute value there mixes coordinate spaces, pushes regionUV far
    // outside [0,1], and (since the height sampler clamps-to-edge) makes
    // every vertex read the same single edge texel: a perfectly flat mesh
    // (confirmed root cause of a real regression, 2026-07-26 — the game's
    // Phase 7 wiring passed the absolute value here while its tree used
    // tnoff_x, silently flattening all terrain while leaving the albedo,
    // which uses its own correctly-computed world_origin_x/z, unaffected —
    // exactly why it looked "textured but flat").
    //
    // Every node later passed to Draw() must lie within this region — this
    // defines the same bounds the caller's TerrainQuadtree::Init()
    // world_size_m must cover, with THAT SAME coordinate-space origin.
    // Returns the real [min,max] height range found so the caller can pass
    // matching values to Draw() for decoding.
    bool Init(int zx0, int zy0, int zone_span, float local_origin_x, float local_origin_z,
              float& out_height_min, float& out_height_max);
    // Re-samples TerrainAtlas over a NEW zone rectangle and replaces just the
    // height texture — releases the old one first. Cheap relative to Init():
    // does NOT recreate the pipeline or unit-patch mesh (those don't depend
    // on which region is loaded). Call whenever the caller's tracked window
    // (e.g. SceneRender::zone_ox/oz) actually moves; Init() must have
    // succeeded once already (ready_ must be true). local_origin_x/z: see
    // Init()'s doc comment — same coordinate-space requirement.
    bool RebuildRegion(int zx0, int zy0, int zone_span, float local_origin_x, float local_origin_z,
                        float& out_height_min, float& out_height_max);

    // Async version of RebuildRegion (2026-07-26, bug #3 third follow-up:
    // "GPU load >80%" + visible seams + still-broken character positioning
    // during fast F3 flythrough). RebuildRegion's CPU cost (measured:
    // 22-90ms depending on window size, TerrainAtlas sampling + RGBA pack)
    // runs SYNCHRONOUSLY on whichever thread calls it — normally the
    // main/render thread, since every real call site (main.cpp's
    // HandleTerrainStreaming/HandleFlythroughStreaming, TerrainStreamQueue::
    // poll, editor's s_rebuild_quadtree_region) is itself on that thread.
    // HandleFlythroughStreaming's own re-centre trigger is only
    // CHUNK_SIZE*0.5=230.4m — at the F3 camera's documented 559 m/s move
    // speed that's a REAL (not redundant — RebuildQuadtreeRegion's
    // idempotency guard, scene_render.h, does nothing here since the window
    // genuinely keeps changing) rebuild roughly every 0.4s, each one a full
    // frame-thread stall. KickRebuildAsync moves ONLY the CPU sampling+pack
    // work (TerrainAtlas reads, RGBA byte packing — no GPU calls, per
    // JobSystem's own contract: "jobs must not touch... GPU resources") to
    // a JobSystem worker; PollRebuildApply, called once per frame from the
    // render thread, does the actual (cheap: ~9-12ms measured) GPU texture
    // upload + swap once the worker's data is ready — the render thread
    // never blocks waiting for TerrainAtlas sampling. Draw() keeps using
    // whatever height_tex_ is currently active (the previous region) for
    // every frame in between; a plain flat/coarse mesh briefly showing the
    // old window's data at the new window's edge is not itself a bug (real
    // engines quietly show "one frame stale" content during streaming) —
    // what SelectVisible/traversal already accepts as "at most one frame
    // stale under load" (terrain_quadtree_async.h) extends here to
    // potentially several frames while a rebuild is in flight, same idea,
    // just a longer window since this work is heavier than a traversal.
    //
    // Returns false immediately (no-op) if a previous KickRebuildAsync's
    // job hasn't finished yet or Init() hasn't succeeded — caller should
    // just try again next frame with the CURRENT window (no queuing: if
    // the window moves again before this one finishes, the in-flight job's
    // now-stale target is simply superseded by the next successful Kick).
    bool KickRebuildAsync(int zx0, int zy0, int zone_span, float local_origin_x, float local_origin_z);
    // Call once per frame from the render thread, before any Draw() calls
    // this frame. If a KickRebuildAsync job has completed since the last
    // poll, applies it now (GPU upload + texture swap, old texture
    // released only after the new one is confirmed live) and writes the
    // new height range to out_height_min/max — caller must copy these into
    // whatever it passes to subsequent Draw() calls this frame (matching
    // RebuildRegion's own out-param contract). Returns false (out params
    // untouched) if nothing was applied this call — the normal case on
    // most frames.
    bool PollRebuildApply(float& out_height_min, float& out_height_max);
    // True while a KickRebuildAsync job is running — callers that need to
    // know whether it's safe to Kick again (KickRebuildAsync already
    // no-ops internally, this is for callers that want to skip the call
    // entirely, e.g. to avoid recomputing zone_span/local_origin for
    // nothing).
    bool RebuildInFlight() const;

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

    // See s_sample_and_pack's own doc comment (terrain_quadtree_renderer.cpp)
    // for the JobSystem "no GPU calls in a job" contract this exists to
    // satisfy — shared between the sync path (Init(), UploadHeightmapRegion)
    // and KickRebuildAsync's worker job.
    static bool s_sample_and_pack(int zx0, int zy0, int zone_span,
                                   float* h_tmp, uint8_t* rgba,
                                   int& out_N, float& out_hmin, float& out_hmax,
                                   float& out_texel_step_m);

    struct RebuildJobArgs {
        int   zx0 = 0, zy0 = 0, zone_span = 0;
        float local_origin_x = 0.f, local_origin_z = 0.f;
        float*   h_tmp = nullptr; // points at owner's h_tmp_ (kMaxRes*kMaxRes)
        uint8_t* rgba  = nullptr; // points at owner's rgba_  (kMaxRes*kMaxRes*4)
        int      out_N = 0;
        float    out_hmin = 0.f, out_hmax = 0.f, out_texel_step_m = 0.f;
        bool     out_ok = false;
    };
    static void s_run_rebuild_job(void* p);

    static constexpr int kPatchN = 64; // 64x64 quads -> 65x65 verts, uint32 IBO
    // Hard cap on region subsample resolution: zone_span<=16 (the editor's
    // widest near-tier window) -> 16*TERRAIN_GRID+1 = 2049 samples/axis.
    // See kZoneGridStep64's doc comment (terrain_quadtree_renderer.cpp) for
    // why this equals TERRAIN_GRID now (was 1025 at half-resolution).
    static constexpr int kMaxRes = 2049;

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

    // Scratch storage for s_sample_and_pack — instance-owned (not function-
    // static) specifically so KickRebuildAsync's worker job can write here
    // concurrently with this instance's OWN synchronous calls never running
    // at the same time (rebuild_in_flight_ guards that), and so two
    // different TerrainQuadtreeRenderer instances in the same process never
    // share one buffer. ~33.6MB combined — same memory this already cost as
    // file-static storage before 2026-07-26's async rework, just owned by
    // the instance now instead of the translation unit.
    float   h_tmp_[kMaxRes * kMaxRes];
    uint8_t rgba_[kMaxRes * kMaxRes * 4];

    RebuildJobArgs rebuild_job_args_;
    mutable SDL_AtomicInt rebuild_in_flight_ = {}; // mutable: SDL_GetAtomicInt takes a non-const pointer
    bool           rebuild_has_pending_result_ = false;

    bool ready_ = false;
};
#endif
