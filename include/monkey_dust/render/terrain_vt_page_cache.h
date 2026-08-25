#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>
#include <cstdint>

class TerrainWorldHeightmap;
class TerrainRenderer;

// terrain-vt Phase 1 (plan at /home/rdga1/.claude/plans/
// serene-pondering-teapot.md): sparse virtual-texturing physical page
// cache for terrain ground shading. A "page" bakes TS_ComputeGroundAlbedo
// (terrain_shading_common.glsl -- the SAME formula the live per-pixel
// screen-space resolve pass uses, via terrain_page_fill.comp, NOT a
// second hand-copied version) ONCE for a PATCH_SIZE x PATCH_SIZE
// world-space square at a given discrete LOD tier, into one PAGE_TEXELS x
// PAGE_TEXELS slot of a single shared physical RGBA8 atlas texture.
//
// Phase 1 scope ONLY: page table + page-fill compute + a fixed-slot-pool
// allocator that REJECTS new requests once full (same non-blocking
// philosophy terrain_stream_queue.h's TerrainStreamQueue::enqueue already
// uses) -- there is deliberately NO eviction yet (Phase 3) and nothing
// yet drives RequestPage from real camera visibility (Phase 2, via
// TerrainPatchGrid::SelectVisible). Phase 1's own verification is a
// debug-only atlas dump (DebugDumpAtlas), not live shading integration
// (Phase 4).
//
// Page grid intentionally matches TerrainPatchGrid's own (ix,iz) grid --
// same patch_size, same (0,0) world origin -- so Phase 2 can derive page
// requests directly from TerrainPatchGrid::VisiblePatch with zero
// coordinate-mapping code.
class TerrainVtPageCache {
public:
    static constexpr int PAGE_TEXELS = 128;  // texel resolution per page; must match terrain_page_fill.comp's PAGE_TEXELS
    static constexpr int SLOTS_X     = 32;
    static constexpr int SLOTS_Y     = 16;
    static constexpr int NUM_SLOTS   = SLOTS_X * SLOTS_Y;  // 512 -- atlas: 4096x2048 RGBA8 = 32MB
    // Bounds how many NEW pages get filled in a single frame -- a camera
    // teleport/fast pan could otherwise reveal far more new pages than a
    // budget-conscious per-frame compute cost should absorb in one go;
    // callers just keep calling RequestPage every frame for pages still
    // wanted, same reject-and-retry-next-frame philosophy as the pool
    // itself.
    static constexpr int MAX_FILLS_PER_FRAME = 16;

    // terrain-vt clipmap fix (this session): the ORIGINAL MIN_CACHEABLE_TIER
    // interim fix (a constant page footprint at every tier, gated off below
    // tier 4) was measured to deliver ZERO frame-time benefit on the actual
    // primary target (Intel HD 520 = RenderTier::Forward, RenderQualityConfig
    // ::terrain_cr_m=3000m): scene_render.cpp's max_lod_cull works out to
    // ~log2(3900/300)=~3.7, so a visible patch NEVER reaches tier 4 on this
    // hardware/setting -- RequestPage's old tier>=4 gate simply never fired,
    // and every visible pixel kept falling back to the full live blend,
    // byte-identical to the pre-VT baseline. Real fix: page world FOOTPRINT
    // now shrinks at finer tiers instead of staying fixed at patch_size_,
    // so near/mid tiers can be cached too without the checkerboard
    // blockiness that originally forced the gate. FINE_SUBDIV=4 means tier
    // 0 subdivides one patch_size_ (300m) cell into a 4x4 grid of 75m
    // sub-pages (2.34m/texel -> 0.586m/texel, 4x finer); tier 1 uses a 2x2
    // grid of 150m sub-pages (1.17m/texel); tier>=2 is unchanged, one
    // page covering the whole 300m cell (2.34m/texel -- already measured
    // imperceptible at that distance before this bug was ever noticed).
    // Conservative, not maximal: a much finer subdivision at tier 0 would
    // look even sharper but multiplies resident-slot pressure (patches near
    // the camera can occupy up to subdiv*subdiv slots each) -- 4x4 keeps
    // worst-case resident count (~9 tier-0 patches * 16 + ~6 tier-1 * 4 +
    // the rest at 1 each, per the live 59-zone stress test's patch-count
    // order of magnitude) comfortably under NUM_SLOTS=512 with headroom for
    // LRU churn. Tune later with live visual + resident-count verification,
    // not by assumption.
    static constexpr int FINE_SUBDIV = 4;
    static int SubdivFactor(int tier) {
        if (tier <= 0) return FINE_SUBDIV;       // 300/4 = 75m footprint
        if (tier == 1) return FINE_SUBDIV / 2;   // 300/2 = 150m footprint
        return 1;                                 // 300m footprint (tier >= 2, unchanged)
    }

    // terrain-vt checkerboard follow-up (earlier session): even with
    // detail-texture aliasing genuinely fixed (page-fill now supersamples,
    // see terrain_page_fill.comp), a live-reported "still checkerboard"
    // report led to isolating a SEPARATE, more fundamental effect via
    // RenderDoc: individual VT pages ARE internally smooth now, but
    // ADJACENT pages (each baked independently, no border/overlap) can
    // legitimately land on quite different average colours -- the detail
    // texture's own repeat period (~55.6m, DETAIL_TILING=90 over
    // TS_CLIFF_UV_SCALE's 5000m) is comparable to tier 0's 75m page
    // footprint, so neighbouring pages sample largely uncorrelated points
    // of it. That session's pragmatic fix was to exclude tier 0 (dist<300m,
    // exactly where a player's eye resolves page boundaries most easily)
    // from caching entirely, gated by this constant at 1.
    //
    // Follow-up (this session): a live FPS report ("40-45 FPS again" close
    // to terrain) traced to exactly that tier-0 exclusion -- confirmed via
    // the engine's own RenderTotal/FenceWait telemetry (RenderTotal jumped
    // 6.8ms->16.8-24.0ms close to terrain, FenceWait ~90% of it, real GPU
    // cost, not a measurement artifact). The cross-page SEAM itself was
    // genuinely fixed (VT_SampleAlbedo in terrain_shading_screenspace.frag
    // now does a manual cross-page bilinear blend across the 4 nearest
    // fine-cell pages -- see its own doc comment) and tier 0 was
    // re-enabled (MIN_CACHEABLE_TIER=0) on that basis -- but a live
    // screenshot at tier 0 (close range) then showed a DIFFERENT, more
    // visible defect the blend does nothing for: within a SINGLE page,
    // VT_SampleCellHeld still does plain texelFetch (nearest-neighbour,
    // no filtering) at tier 0's 0.586m/texel spacing -- at tiers 1-3 that
    // texel size is sub-pixel from typical camera distance and invisible,
    // but at tier 0's close range it reads as an obvious quilted/checker
    // grid across any nearby slope (confirmed live, screenshot
    // 2026-08-06). The cross-page blend's weight barely moves within a
    // single page's interior (it only sweeps a full 0-1 range across one
    // whole 75m fine cell), so it does not hide this. Reverted to 1 as a
    // stopgap while a real fix was worked out.
    //
    // Second pass (this session): added real hardware bilinear WITHIN a
    // page too (VT_SampleCellHeld now samples via textureLod with a
    // hand-clamped half-texel-inset UV instead of texelFetch -- see its
    // own doc comment), which does genuinely turn the blocky grid smooth.
    // Re-enabled tier 0 on that basis -- but a live screenshot comparison
    // against the ORIGINAL live-shaded (uncached) rendering at the same
    // spot showed the smoothing had traded blockiness for a DIFFERENT
    // real loss: bilinear interpolation between adjacent 0.586m/texel VT
    // samples cannot recover detail that was never baked at that
    // resolution in the first place -- the live path samples the actual
    // detail texture (DETAIL_TEX_RESOLUTION=2048 texels/layer,
    // DETAIL_TILING=90 over a much larger world area) essentially per-
    // pixel; tier 0's page bake is ~3-4 orders of magnitude coarser than
    // that. No amount of filtering (cross-page OR within-page) can put
    // back information the bake step already threw away -- "smooth" here
    // just meant "smoothly blurry" instead of "blockily blurry", not
    // "as detailed as live shading". A real fix would mean baking tier 0
    // at a much finer PAGE_TEXELS or smaller sub-page footprint (real
    // memory/compute cost increase), not attempted here. Reverted to 1 --
    // tier 0 stays live (full detail, no caching benefit); the cross-page
    // (VT_SampleAlbedo) and within-page (VT_SampleCellHeld) smoothing
    // fixes both stay in place and are real correctness improvements
    // whenever the camera is close enough to a tier 1-3 page boundary to
    // notice it, they just don't get exercised at tier 0 while this is 1.
    // VT caching disabled entirely (2026-08-09, user directive) -- tier-
    // boundary seam artifacts (hard sharpness discontinuities between
    // adjacent cached pages, visible live in the editor 3D World view and
    // in-game) were never fully resolved despite Phases 0-4 + 4 tier-0
    // attempts. No tier ever qualifies as cacheable now (max tier is 7,
    // see terrain_patch_renderer.h's tier_n_ comment) -- every pixel goes
    // through the live per-pixel shading path unconditionally, same as
    // before the VT system existed. Not reverting/deleting the VT code
    // itself (page table, page-fill compute, cache eviction) -- just
    // gating it fully off so it costs nothing and touches nothing.
    //
    // Revival attempt (2026-08-24): re-enabled (MIN_CACHEABLE_TIER=1) and
    // wired to TerrainQuadtree via a new RequestVisibleNode() bridge.
    // Functionally correct (real, non-garbage ground shading verified live
    // via game_cmd_driver screenshots; 70+ resident pages, 0 evictions) but
    // REVERTED again same session: live GpuProfiler telemetry showed
    // "Terrain VT Fill" costing ~1.5-4ms EVERY frame even with a fully
    // static camera held for 25+ seconds -- never converging toward the
    // near-zero steady-state cost the whole feature depends on for its
    // payoff. FenceWait stayed ~17ms, no better than the pre-VT ~14-15ms
    // baseline. Root cause not isolated before the revert decision (leading
    // suspect: TerrainQuadtree::SelectVisible has no persistent tree state
    // and recomputes fresh every frame from camera+frustum -- RequestPage's
    // tier assignment for a boundary-adjacent node can flip frame-to-frame
    // even for a "static" camera, and RequestSubtile's own doc comment
    // already describes this exact same-corner tier-flap class of problem
    // from the ORIGINAL TerrainPatchGrid-driven design, where each flip
    // forces a real re-fill dispatch, not a no-op). Whoever revisits this:
    // start there, and get real tier-flip counts (not just resident/
    // eviction) before re-attempting a live wire-up.
    static constexpr int MIN_CACHEABLE_TIER = 99;

    // Symmetric upper bound (this session, editor 3D World investigation):
    // the game's own SceneRender::UpdateGraniteTerrain never asks for a
    // tier above ~3 in the first place (RenderQualityConfig::terrain_cr_m
    // ~3000m converts to max_lod_cull=log2(3000*1.3/300)~=3.7, so
    // SelectVisible's frustum-plus-far-cull already discards anything
    // coarser before RequestPage ever sees it -- this is WHY the doc
    // comments above keep saying "tiers 1-3 are confirmed reachable,
    // vast majority of any view" specifically for the primary in-game
    // target). NUM_SLOTS=512 was sized against that same real-world
    // envelope (~130-140 patches across those 4 tiers, see FINE_SUBDIV's
    // own doc comment). The editor's World3D viewport (editor_world_3d_
    // sdlgpu.cpp) is a deliberately unbounded wide-overview tool -- its
    // own SelectVisible call passes no max_lod_cull, so at a few km of
    // altitude it legitimately sees patches at tiers up to kNumTiers-1=7
    // stretching to the world's edge. Requesting VT pages for ALL of
    // those overwhelmed the 512-slot budget (confirmed live: rectangular
    // stale/wrong-tier patches at altitude, screenshots 2026-08-07) --
    // classic thrashing where the working set has no locality (a wide
    // pan never revisits the same far tile soon enough for its cached
    // page to still be useful). Gating RequestPage itself (not each call
    // site) bounds worst-case resident pages to the same order of
    // magnitude the cache was actually tuned for, in BOTH the game and
    // the editor, regardless of how far out any given viewport's own
    // visibility cull happens to reach. Tiers above this fall back to the
    // live per-pixel blend -- same self-healing miss-fallback tier 0
    // already relies on, not a new code path.
    static constexpr int MAX_CACHEABLE_TIER = 3;

    // Indirection texture side length in texels, now at the FINEST
    // granularity (patch_size_/FINE_SUBDIV = 75m per texel) since that's
    // the smallest page footprint any tier can use -- coarser tiers just
    // write the same slot value into a span x span block of texels (see
    // RequestPage/UploadIndirectionRegion). 512 = old INDIR_SIZE(128) *
    // FINE_SUBDIV(4), covering the same real world extent (~29491.2m) at
    // 4x finer addressing.
    static constexpr int INDIR_SIZE  = 128 * FINE_SUBDIV;
    // Indirection texel value meaning "no page resident here".
    static constexpr uint32_t kNotResident = 0xFFFFFFFFu;

    // patch_size: world-space edge length of one page (match whatever
    // TerrainPatchGrid instance Phase 2 will read visibility from --
    // TerrainPatchGrid::PatchSize()). hmap supplies world_extent/height
    // range/heightmap resolution, cached once (this cache assumes ONE
    // TerrainWorldHeightmap instance for its whole lifetime, same as
    // TerrainPatchGrid itself).
    bool Init(SDL_GPUDevice* dev, float patch_size, const TerrainWorldHeightmap& hmap);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    SDL_GPUTexture* AtlasTexture() const { return atlas_tex_; }
    SDL_GPUSampler* AtlasSampler() const { return atlas_sampler_; }
    SDL_GPUTexture* IndirectionTexture() const { return indir_tex_; }
    // Semantically irrelevant (Phase 4's consumer only ever reads this
    // texture via texelFetch, which ignores sampler state entirely) but
    // SDL_GPU still requires a valid non-null sampler object to bind a
    // combined-image-sampler resource -- a plain NEAREST/CLAMP sampler.
    SDL_GPUSampler* IndirectionSampler() const { return indir_sampler_; }
    float PatchSize() const { return patch_size_; }

    // Phase 2 (CPU visibility feedback) calls this once per visible
    // TerrainPatchGrid patch (patch_ix,patch_iz), per frame, with that
    // patch's assigned tier -- signature unchanged since before the
    // clipmap fix, so callers (scene_render.cpp/editor_world_3d_sdlgpu.cpp)
    // need no changes beyond removing the old MIN_CACHEABLE_TIER gate.
    // Internally expands into SubdivFactor(tier)^2 sub-page requests (1 for
    // tier>=2, 4 for tier 1, 16 for tier 0) -- see FINE_SUBDIV's doc
    // comment. Residency key for each sub-page is its own fine-grid corner
    // (fine_ix,fine_iz) at FINE_SUBDIV granularity, NOT (patch_ix,patch_iz)
    // -- the indirection texture has exactly one texel per fine-grid
    // coordinate, so it can only ever point at ONE page's slot for a given
    // fine location at a time (same reasoning the original single-tier
    // design already established for why tier must never be folded into
    // the residency key -- still true here, just at finer granularity).
    //
    // If a sub-page's fine corner is already resident at the SAME span:
    // just bumps LRU recency, no-op otherwise. If resident at a DIFFERENT
    // span (this patch's tier changed since last frame): reuses the SAME
    // slot and queues a re-fill. If not resident at all: allocates a free
    // slot (evicting the globally least-recently-touched one if the pool
    // is full) and queues a page-fill compute dispatch for THIS frame
    // (drained by FlushFillQueue) -- unless this frame's fill queue is
    // already at MAX_FILLS_PER_FRAME, in which case this sub-page request
    // is a silent no-op (reject-on-full; caller just calls RequestPage
    // again next frame for any patch still wanted -- a tier-0 patch whose
    // 16 sub-pages can't all fit this frame's budget just converges over
    // a few frames, falling back to the full live blend meanwhile, same
    // self-healing behavior the original single-page design already had).
    //
    // A tier transition (e.g. camera recedes from tier 0 to tier 2 over
    // this same patch) leaves the OLD, now-unreferenced fine sub-page
    // slots marked used/resident but unreachable via indirection (the new,
    // coarser request only rewrites the fine cells it actually spans) --
    // not a correctness bug, just delayed slot reclamation: AllocSlot's
    // LRU eviction reclaims them once the pool actually needs the space,
    // same tolerance this cache already has for any other stale-but-valid
    // resident content.
    void RequestPage(int patch_ix, int patch_iz, int tier);

    // Issues this frame's queued page-fill compute dispatches (raw
    // SDL_BeginGPUComputePass, not GpuComputePass -- see
    // terrain_page_fill.comp's doc comment on why: GpuComputePass::Begin
    // hardcodes storage-texture bindings to nullptr,0), uploads each new
    // page's indirection region + PageMetaSSBO entry via a copy pass on
    // the SAME command buffer. Must run OUTSIDE any render/compute pass
    // already open this frame, BEFORE any pass that reads the atlas/
    // indirection/page-meta resources. Clears the fill queue on return
    // regardless of success. `ground` supplies the same 4 shared ground
    // samplers + zoneGroundLayers SSBO TerrainShadingProjected::
    // DrawShadingResolve already binds (TerrainRenderer::
    // GetSharedGroundSamplers/ZoneGroundLayersSSBO) -- page-fill reuses
    // them rather than owning a second copy.
    void FlushFillQueue(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd,
                         const TerrainWorldHeightmap& hmap, const TerrainRenderer& ground);

    // Debug-only (Phase 1 verification): dump the whole atlas to a PNG via
    // a blocking SDL_DownloadFromGPUTexture readback (same pattern
    // tools/editor/editor_screenshot.cpp already uses for the swapchain)
    // -- NOT a per-frame path.
    bool DebugDumpAtlas(SDL_GPUDevice* dev, const char* out_png_path);

    // terrain-vt clipmap fix: per-slot (origin_x, origin_z, world_size, 0)
    // metadata, indexed by slot -- VT_SampleAlbedo (terrain_shading_
    // screenspace.frag) reads this AFTER resolving a slot from the
    // indirection texture to compute the correct in-page UV, since a
    // resident page's real-world footprint is no longer always
    // patch_size_ (300m). Deliberately a raw, single-buffered SDL_GPUBuffer
    // (NOT the triple-buffered SSBO wrapper class) -- same reasoning as
    // indir_tex_ itself: this must stay byte-for-byte in sync with
    // whatever the indirection texture points at THIS frame, and a
    // triple-buffered ring would serve a stale (2-frames-old) slot's
    // metadata for 2 out of every 3 frames whenever a page is (re)filled.
    SDL_GPUBuffer* PageMetaSSBO() const { return page_meta_buf_; }

    int ResidentCount() const { return resident_count_; }
    // terrain-vt Phase 3: total evictions since Init() -- 0 for the whole
    // lifetime of a session means NUM_SLOTS never needed to reclaim a slot
    // (matches Phase 1's own "empirical dial, not a hard number" framing).
    uint64_t EvictionCount() const { return eviction_count_; }

private:
    struct SlotInfo {
        // Fine-grid (FINE_SUBDIV-per-patch-edge) corner coordinate this
        // slot's page occupies, and how many fine cells per edge it spans
        // (1 for tier>=2's 300m footprint, 2 for tier 1's 150m, 4 for tier
        // 0's 75m) -- together these fully describe the page's real-world
        // origin/size (origin = fine_ix*FineCell(), size = span*FineCell()).
        int      fine_ix = 0, fine_iz = 0, span = 4, tier = 0;
        bool     used = false;
        // terrain-vt Phase 3: monotonic per-touch counter, NOT a
        // frame-granularity timestamp -- the documented LRU bug in the
        // deleted TerrainBakedRenderer used a per-frame value, so every
        // slot touched during the SAME frame tied and eviction always
        // picked the same one regardless of true recency. A counter that
        // increments on every single touch (RequestPage hit OR (re)alloc)
        // makes same-frame ties impossible by construction.
        uint64_t last_touch = 0;
    };
    struct FillRequest {
        int fine_ix = 0, fine_iz = 0, span = 4, tier = 0, slot = 0;
    };
    // terrain-vt Phase 3: when AllocSlot() evicts an in-use slot for a new
    // page, the OLD page's indirection region must be invalidated back to
    // kNotResident in the SAME flush -- otherwise the indirection texture
    // keeps claiming the old fine-grid region still lives in a slot that
    // now holds a completely different page's content.
    struct InvalidateRequest {
        int fine_ix = 0, fine_iz = 0, span = 4;
    };

    float FineCell() const { return patch_size_ / (float)FINE_SUBDIV; }
    // Expands one visible-patch RequestPage call into SubdivFactor(tier)^2
    // sub-page requests -- see RequestPage's own doc comment.
    void RequestSubtile(int fine_ix, int fine_iz, int span, int tier);
    // Residency key is (fine_ix,fine_iz) only -- see RequestPage's doc
    // comment for why tier/span are deliberately NOT part of this lookup.
    int FindSlot(int fine_ix, int fine_iz) const;
    int AllocSlot();
    void UploadIndirectionRegion(SDL_GPUDevice* dev, SDL_GPUCopyPass* cp,
                                  int fine_ix, int fine_iz, int span, uint32_t value);
    void UploadPageMeta(SDL_GPUDevice* dev, SDL_GPUCopyPass* cp, int slot,
                         float origin_x, float origin_z, float world_size);

    SDL_GPUTexture*    atlas_tex_     = nullptr;
    SDL_GPUSampler*    atlas_sampler_ = nullptr;
    SDL_GPUTexture*    indir_tex_     = nullptr;
    SDL_GPUSampler*    indir_sampler_ = nullptr;
    SDL_GPUBuffer*     page_meta_buf_ = nullptr;
    GpuComputePipeline fill_pipeline_;

    SlotInfo slots_[NUM_SLOTS];
    int      resident_count_ = 0;
    uint64_t touch_counter_  = 0;
    uint64_t eviction_count_ = 0;

    FillRequest fill_queue_[MAX_FILLS_PER_FRAME];
    int         fill_queue_count_ = 0;
    InvalidateRequest invalidate_queue_[MAX_FILLS_PER_FRAME];
    int                invalidate_queue_count_ = 0;

    float patch_size_   = 300.f;
    float world_extent_ = 0.f, height_min_ = 0.f, height_max_ = 0.f, res_texels_ = 0.f;

    bool ready_ = false;
};
#endif
