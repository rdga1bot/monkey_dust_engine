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
    // Indirection texture side length in texels -- one texel per possible
    // (ix,iz) page-grid coordinate, clamped. 128 covers this world's real
    // patch-grid extent (~99x99 at patch_size=300m, world_extent=29491.2m)
    // with headroom for a smaller patch_size chosen later.
    static constexpr int INDIR_SIZE  = 128;
    // Bounds how many NEW pages get filled in a single frame -- a camera
    // teleport/fast pan could otherwise reveal far more new pages than a
    // budget-conscious per-frame compute cost should absorb in one go;
    // callers just keep calling RequestPage every frame for pages still
    // wanted, same reject-and-retry-next-frame philosophy as the pool
    // itself.
    static constexpr int MAX_FILLS_PER_FRAME = 16;
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
    float PatchSize() const { return patch_size_; }

    // Phase 2 (CPU visibility feedback) calls this once per visible patch,
    // per frame. Residency key is (ix,iz) ONLY, NOT (ix,iz,tier) -- the
    // indirection texture has exactly one texel per (ix,iz), so it can
    // only ever point at ONE tier's slot for a given location at a time.
    // Keying on the triple (an earlier draft of this class did) let two
    // different tiers of the same location occupy two different slots
    // simultaneously with only the more-recently-requested one actually
    // reachable via indirection -- the other became a silently orphaned
    // slot, and worse, evicting it later would have invalidated the
    // WRONG (currently-valid) indirection entry, since both slots shared
    // the same (ix,iz) indirection texel address. Fixed before ever
    // shipping any consumer of this class.
    //
    // If (ix,iz) is already resident at the SAME tier: just bumps LRU
    // recency, no-op otherwise. If resident at a DIFFERENT tier: reuses
    // the SAME slot (no new allocation, no indirection re-upload needed --
    // it already points here) and queues a re-fill with the new tier's
    // content. If not resident at all: allocates a free slot (evicting the
    // globally least-recently-touched one if the pool is full) and queues
    // a page-fill compute dispatch for THIS frame (drained by
    // FlushFillQueue) -- unless this frame's fill queue is already at
    // MAX_FILLS_PER_FRAME, in which case this call is a silent no-op
    // (reject-on-full; caller just calls again next frame if the page is
    // still wanted).
    void RequestPage(int ix, int iz, int tier);

    // Issues this frame's queued page-fill compute dispatches (raw
    // SDL_BeginGPUComputePass, not GpuComputePass -- see
    // terrain_page_fill.comp's doc comment on why: GpuComputePass::Begin
    // hardcodes storage-texture bindings to nullptr,0) and uploads each
    // new page's indirection texel via a copy pass on the SAME command
    // buffer. Must run OUTSIDE any render/compute pass already open this
    // frame, BEFORE any pass that reads the atlas/indirection textures.
    // Clears the fill queue on return regardless of success. `ground`
    // supplies the same 4 shared ground samplers + zoneGroundLayers SSBO
    // TerrainShadingProjected::DrawShadingResolve already binds
    // (TerrainRenderer::GetSharedGroundSamplers/ZoneGroundLayersSSBO) --
    // page-fill reuses them rather than owning a second copy.
    void FlushFillQueue(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd,
                         const TerrainWorldHeightmap& hmap, const TerrainRenderer& ground);

    // Debug-only (Phase 1 verification): dump the whole atlas to a PNG via
    // a blocking SDL_DownloadFromGPUTexture readback (same pattern
    // tools/editor/editor_screenshot.cpp already uses for the swapchain)
    // -- NOT a per-frame path.
    bool DebugDumpAtlas(SDL_GPUDevice* dev, const char* out_png_path);

    int ResidentCount() const { return resident_count_; }
    // terrain-vt Phase 3: total evictions since Init() -- 0 for the whole
    // lifetime of a session means NUM_SLOTS never needed to reclaim a slot
    // (matches Phase 1's own "empirical dial, not a hard number" framing).
    uint64_t EvictionCount() const { return eviction_count_; }

private:
    struct SlotInfo {
        int      ix = 0, iz = 0, tier = 0;
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
        int ix = 0, iz = 0, tier = 0, slot = 0;
    };
    // terrain-vt Phase 3: when AllocSlot() evicts an in-use slot for a new
    // page, the OLD page's indirection texel must be invalidated back to
    // kNotResident in the SAME flush -- otherwise the indirection texture
    // keeps claiming the old (ix,iz,tier) key still lives in a slot that
    // now holds a completely different page's content.
    struct InvalidateRequest {
        int ix = 0, iz = 0;
    };

    // Residency key is (ix,iz) only -- see RequestPage's doc comment for
    // why tier is deliberately NOT part of this lookup.
    int FindSlot(int ix, int iz) const;
    int AllocSlot();
    void UploadIndirectionTexel(SDL_GPUDevice* dev, SDL_GPUCopyPass* cp, int ix, int iz, uint32_t value);

    SDL_GPUTexture*    atlas_tex_     = nullptr;
    SDL_GPUSampler*    atlas_sampler_ = nullptr;
    SDL_GPUTexture*    indir_tex_     = nullptr;
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
