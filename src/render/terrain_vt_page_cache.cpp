#include <monkey_dust/render/terrain_vt_page_cache.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/terrain_world_heightmap.h>
#include <monkey_dust/render/terrain_renderer.h>
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>
#include <cstdlib>
#include "stb_image_write.h"

namespace {
// Mirrors terrain_page_fill.comp's PageFillUBO exactly (std140 layout) --
// field ORDER matters, not just presence: the real fields this shader
// reads (world_params/hmap_world/page_world/slot_offset) come FIRST,
// before the dead sun_dir_str/ambient/fog_color_near/fog_far/_pad tail
// (see that shader's doc comment for why they still have to exist at
// all). Root-caused live: with the dead fields FIRST (original attempt),
// std140's vec3-alignment rule for `_pad` silently inserted a padding gap
// this hand-written C++ struct didn't mirror, shifting every REAL field
// after it by 16 bytes relative to what the shader actually read -- every
// dispatch behaved as if slot_offset was always (0,0) regardless of what
// was pushed. Putting the vec3 tail LAST sidesteps the whole class of
// bug: nothing reads past it, so its exact padding no longer matters.
struct PageFillUBO {
    float   world_params[4];
    float   hmap_world[4];
    float   page_world[4];
    int32_t slot_offset[4];
    float   sun_dir_str[4];
    float   ambient[4];
    float   fog_color_near[4];
    float   fog_far;
    float   _pad[7];  // covers std140's implicit vec3-alignment gap + trailing struct round-up (28 bytes)
};
static_assert(sizeof(PageFillUBO) == 144, "PageFillUBO must match terrain_page_fill.comp's real std140 size");
} // namespace

bool TerrainVtPageCache::Init(SDL_GPUDevice* dev, float patch_size, const TerrainWorldHeightmap& hmap) {
    if (!dev || !hmap.IsReady()) {
        MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] Init: invalid device or heightmap not ready");
        return false;
    }
    // audit S2-11 (2026-08-27) + follow-up fix (2026-08-27, same day): MIN_
    // CACHEABLE_TIER=99 (its own doc comment, "VT caching disabled
    // entirely") guarantees RequestPage's tier gate rejects every possible
    // call -- MAX_CACHEABLE_TIER=3 is the highest tier ever requested
    // anyway, so 99 can never be satisfied. Allocating the full 33MB
    // atlas+indirection for a cache that can never hold a page is pure
    // waste on an iGPU sharing memory bandwidth with the CPU.
    //
    // The ORIGINAL S2-11 fix returned false here with NO resources
    // allocated at all, on the claim that TerrainShadingProjected::
    // DrawShadingResolve's `if (!vt.IsReady()) return;` gate would just
    // skip "the 3 VT bindings". That claim was never verified live and was
    // wrong: that gate is the FIRST line of the function, before the
    // fullscreen-triangle draw call that actually resolves terrain shading
    // to the screen -- so a permanently-false IsReady() made terrain
    // render literally nothing every frame (G-buffer fill still ran and
    // cost real GPU time, but the resolve pass that turns it into visible
    // pixels never executed). Root-caused via git bisect + live screenshot
    // classification after the regression shipped.
    //
    // Fix: still skip the real (33MB) allocation, but always create
    // minimal 1x1/1-slot fallback resources below so every accessor
    // (AtlasTexture/IndirectionTexture/PageMetaSSBO) returns a valid,
    // bindable object regardless of whether real caching is enabled.
    // ready_ stays false (RequestPage/FlushFillQueue etc. remain fully
    // inert, unchanged from before this fix) -- only the bindability
    // contract changes.
    if (MIN_CACHEABLE_TIER > MAX_CACHEABLE_TIER) {
        return InitDisabledFallback(dev);
    }
    patch_size_   = patch_size;
    world_extent_ = hmap.WorldExtent();
    height_min_   = hmap.HeightMin();
    height_max_   = hmap.HeightMax();
    res_texels_   = (float)hmap.Resolution();

    // Physical atlas: uncompressed RGBA8 -- BC3/compressed formats cannot
    // be a compute imageStore target in SDL_GPU/Vulkan (confirmed by
    // evsm_shadow.cpp's own doc comment: Intel ANV crashes on
    // SDL_CreateGPUTexture with a compressed format + COMPUTE_STORAGE_WRITE).
    {
        GpuSamplerDesc atlas_sdesc;
        atlas_sdesc.min_filter = GpuSamplerDesc::Filter::LINEAR;
        atlas_sdesc.mag_filter = GpuSamplerDesc::Filter::LINEAR;
        atlas_sdesc.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        atlas_sdesc.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        atlas_sdesc.gen_mipmap = false;
        if (!atlas_tex_.InitCompute((int)(SLOTS_X * PAGE_TEXELS), (int)(SLOTS_Y * PAGE_TEXELS),
                                     SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                     SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                     1, atlas_sdesc)) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] atlas texture create failed: %s", SDL_GetError());
            return false;
        }
        if (!atlas_tex_.SDLSampler()) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] atlas sampler create failed: %s", SDL_GetError());
            atlas_tex_.Shutdown();
            return false;
        }
    }

    // Indirection texture: R32_UINT, one texel per FINE-grid (FINE_SUBDIV
    // per patch edge) coordinate -- CPU-written via a copy pass
    // (SDL_UploadToGPUTexture), never a compute imageStore target, so no
    // COMPUTE_STORAGE_WRITE usage needed here.
    {
        GpuSamplerDesc indir_sdesc;
        indir_sdesc.min_filter = GpuSamplerDesc::Filter::NEAREST;
        indir_sdesc.mag_filter = GpuSamplerDesc::Filter::NEAREST;
        indir_sdesc.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        indir_sdesc.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        indir_sdesc.gen_mipmap = false;
        if (!indir_tex_.InitCompute((int)INDIR_SIZE, (int)INDIR_SIZE, SDL_GPU_TEXTUREFORMAT_R32_UINT,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, indir_sdesc)) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] indirection texture create failed: %s", SDL_GetError());
            atlas_tex_.Shutdown();
            return false;
        }
        if (!indir_tex_.SDLSampler()) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] indirection sampler create failed: %s", SDL_GetError());
            indir_tex_.Shutdown();
            atlas_tex_.Shutdown();
            return false;
        }
    }
    // Initialize every indirection texel to kNotResident -- one big upload,
    // once, at Init() time (INDIR_SIZE^2 = 16384 uint32 = 64KB, trivial).
    {
        uint32_t* init_data = (uint32_t*)malloc((size_t)INDIR_SIZE * INDIR_SIZE * sizeof(uint32_t));
        if (!init_data) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] indirection init malloc failed");
            return false;
        }
        for (int i = 0; i < INDIR_SIZE * INDIR_SIZE; ++i) init_data[i] = kNotResident;

        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size  = (Uint32)(INDIR_SIZE * INDIR_SIZE * sizeof(uint32_t));
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
        if (tb) {
            void* map = GpuMapTransfer(tb, false);
            if (map) memcpy(map, init_data, tbi.size);
            GpuUnmapTransfer(tb);

            SDL_GPUCommandBuffer* cmd = md::GpuDevice::Get().AcquireCommandBuffer();
            GpuCopyPass cp;
            cp.Begin(cmd);
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = tb;
            src.pixels_per_row  = (Uint32)INDIR_SIZE;
            src.rows_per_layer  = (Uint32)INDIR_SIZE;
            SDL_GPUTextureRegion dst{};
            dst.texture = indir_tex_.SDLTexture();
            dst.w = (Uint32)INDIR_SIZE; dst.h = (Uint32)INDIR_SIZE; dst.d = 1;
            cp.UploadTexture(src, dst, false);
            cp.End();
            md::GpuDevice::Get().Submit(cmd);
            SDL_ReleaseGPUTransferBuffer(dev, tb);
        }
        free(init_data);
    }

    // terrain-vt clipmap fix: per-slot page metadata (origin_x, origin_z,
    // world_size, 0), read by VT_SampleAlbedo after resolving a slot from
    // the indirection texture. Raw, single-buffered SDL_GPUBuffer -- see
    // PageMetaSSBO's doc comment for why the triple-buffered SSBO wrapper
    // class would serve stale metadata 2 out of 3 frames. Same usage flags
    // TerrainRenderer's zone_layers_ssbo_ (SSBO class) already proves work
    // fine for BOTH a compute-storage-buffer read (terrain_page_fill.comp
    // doesn't actually need this one, but mirrors the flag set) and a
    // fragment-storage-buffer bind (terrain_shading_screenspace.frag).
    {
        // Zero-init every slot's metadata (world_size defaults to
        // patch_size_ -- harmless, since a slot's meta is only ever read
        // when the indirection texture actually points a resident fine
        // cell at it, which always happens together with a real
        // UploadPageMeta call for that same slot).
        const uint32_t meta_size = (uint32_t)(NUM_SLOTS * 4 * sizeof(float));
        float* init_meta = (float*)malloc((size_t)NUM_SLOTS * 4 * sizeof(float));
        if (init_meta) {
            for (int i = 0; i < NUM_SLOTS; ++i) {
                init_meta[i * 4 + 0] = 0.f;
                init_meta[i * 4 + 1] = 0.f;
                init_meta[i * 4 + 2] = patch_size_;
                init_meta[i * 4 + 3] = 0.f;
            }
            page_meta_buf_.Init(GPU_TARGET_COMPUTE_STORAGE_RW, init_meta, meta_size);
            free(init_meta);
        } else {
            // malloc failure fallback: still create the buffer (matches
            // pre-migration behavior, which created it unconditionally
            // BEFORE attempting the zero-init upload) -- just uninitialized
            // instead of zeroed, rather than failing Init() outright.
            page_meta_buf_.InitEmpty(GPU_TARGET_COMPUTE_STORAGE_RW, meta_size);
        }
        if (!page_meta_buf_.SDLBuffer()) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] page-meta buffer create failed: %s", SDL_GetError());
            return false;
        }
    }

    {
        GpuComputePipeline::Desc cd;
        cd.glsl_path                     = "shaders/terrain_page_fill.comp";
        cd.num_samplers                  = 6;  // heightTex, tex_colour, tex_ground, tex_ground_baked, tex_overlay_mask, zoneGroundLayersTex (texture not SSBO since 2026-08-09)
        cd.num_readonly_storage_buffers  = 0;
        cd.num_readwrite_storage_textures = 1; // pageAtlas
        cd.num_uniform_buffers           = 1;  // PageFillUBO
        cd.threadcount_x = 8;
        cd.threadcount_y = 8;
        cd.threadcount_z = 1;
        if (!fill_pipeline_.Create(cd)) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] page-fill compute pipeline create failed");
            return false;
        }
    }

    for (auto& s : slots_) s = SlotInfo{};
    resident_count_   = 0;
    fill_queue_count_ = 0;
    ready_ = true;
    MD_LOG(MD_LOG_INFO, "[TerrainVtPageCache] ready: %dx%d atlas (%d slots), %dx%d indirection, patch_size=%.1f",
           SLOTS_X * PAGE_TEXELS, SLOTS_Y * PAGE_TEXELS, NUM_SLOTS, INDIR_SIZE, INDIR_SIZE, patch_size_);
    return true;
}

bool TerrainVtPageCache::InitDisabledFallback(SDL_GPUDevice* dev) {
    // 1x1 atlas: same format/usage contract as the real atlas (minus
    // COMPUTE_STORAGE_WRITE, since a disabled cache is never written by
    // terrain_page_fill.comp) so DrawShadingResolve's binding code path is
    // completely unchanged whether VT is enabled or not.
    {
        GpuSamplerDesc atlas_sdesc;
        atlas_sdesc.min_filter = GpuSamplerDesc::Filter::NEAREST;
        atlas_sdesc.mag_filter = GpuSamplerDesc::Filter::NEAREST;
        atlas_sdesc.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        atlas_sdesc.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        atlas_sdesc.gen_mipmap = false;
        if (!atlas_tex_.InitCompute(1, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, atlas_sdesc)) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] disabled-fallback atlas texture create failed: %s", SDL_GetError());
            return false;
        }
        if (!atlas_tex_.SDLSampler()) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] disabled-fallback atlas sampler create failed: %s", SDL_GetError());
            atlas_tex_.Shutdown();
            return false;
        }
    }
    // 1x1 indirection, initialized to kNotResident -- VT_SampleAlbedo (the
    // shader side) reads this first and falls back to the live per-pixel
    // path whenever a texel says "not resident", which is now the ONLY
    // value this texture can ever contain.
    {
        GpuSamplerDesc indir_sdesc;
        indir_sdesc.min_filter = GpuSamplerDesc::Filter::NEAREST;
        indir_sdesc.mag_filter = GpuSamplerDesc::Filter::NEAREST;
        indir_sdesc.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        indir_sdesc.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        indir_sdesc.gen_mipmap = false;
        if (!indir_tex_.InitCompute(1, 1, SDL_GPU_TEXTUREFORMAT_R32_UINT,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, indir_sdesc)) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] disabled-fallback indirection texture create failed: %s", SDL_GetError());
            atlas_tex_.Shutdown();
            return false;
        }
        if (!indir_tex_.SDLSampler()) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] disabled-fallback indirection sampler create failed: %s", SDL_GetError());
            indir_tex_.Shutdown();
            atlas_tex_.Shutdown();
            return false;
        }
    }
    {
        uint32_t not_resident = kNotResident;
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size  = (Uint32)sizeof(uint32_t);
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
        if (tb) {
            void* map = GpuMapTransfer(tb, false);
            if (map) memcpy(map, &not_resident, sizeof(not_resident));
            GpuUnmapTransfer(tb);

            SDL_GPUCommandBuffer* cmd = md::GpuDevice::Get().AcquireCommandBuffer();
            GpuCopyPass cp;
            cp.Begin(cmd);
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = tb;
            src.pixels_per_row  = 1;
            src.rows_per_layer  = 1;
            SDL_GPUTextureRegion dst{};
            dst.texture = indir_tex_.SDLTexture();
            dst.w = 1; dst.h = 1; dst.d = 1;
            cp.UploadTexture(src, dst, false);
            cp.End();
            md::GpuDevice::Get().Submit(cmd);
            SDL_ReleaseGPUTransferBuffer(dev, tb);
        }
    }
    // 1-slot page-meta buffer -- never actually indexed (indirection always
    // says kNotResident), just needs to be a valid bindable SSBO.
    {
        page_meta_buf_.InitEmpty(GPU_TARGET_COMPUTE_STORAGE_RW, (uint32_t)(4 * sizeof(float)));
        if (!page_meta_buf_.SDLBuffer()) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] disabled-fallback page-meta buffer create failed: %s", SDL_GetError());
            return false;
        }
    }
    ready_ = false;  // page-management (RequestPage/FlushFillQueue) stays fully inert
    MD_LOG(MD_LOG_INFO, "[TerrainVtPageCache] disabled (MIN_CACHEABLE_TIER=%d > MAX_CACHEABLE_TIER=%d) -- 1x1 fallback resources only, real caching inert",
           MIN_CACHEABLE_TIER, MAX_CACHEABLE_TIER);
    return true;
}

void TerrainVtPageCache::Shutdown(SDL_GPUDevice* dev) {
    if (!dev) return;
    fill_pipeline_.Destroy();
    atlas_tex_.Shutdown();
    indir_tex_.Shutdown();
    page_meta_buf_.Shutdown();
    ready_ = false;
}

int TerrainVtPageCache::FindSlot(int fine_ix, int fine_iz) const {
    for (int i = 0; i < NUM_SLOTS; ++i) {
        const SlotInfo& s = slots_[i];
        if (s.used && s.fine_ix == fine_ix && s.fine_iz == fine_iz) return i;
    }
    return -1;
}

int TerrainVtPageCache::AllocSlot() {
    for (int i = 0; i < NUM_SLOTS; ++i) {
        if (!slots_[i].used) return i;
    }
    // Pool full -- evict the least-recently-touched slot (linear scan over
    // last_touch; NUM_SLOTS=512 is small enough that this is trivially
    // cheap even called once per newly-seen page per frame -- no
    // std::map/priority-queue needed, matches this codebase's fixed-array
    // convention). last_touch is a monotonic per-touch counter, not a
    // frame timestamp -- see SlotInfo's own doc comment for why that
    // distinction is the actual fix, not a stylistic preference.
    int lru = 0;
    uint64_t oldest = slots_[0].last_touch;
    for (int i = 1; i < NUM_SLOTS; ++i) {
        if (slots_[i].last_touch < oldest) { oldest = slots_[i].last_touch; lru = i; }
    }
    ++eviction_count_;
    return lru;
}

void TerrainVtPageCache::RequestPage(int patch_ix, int patch_iz, int tier) {
    if (!ready_) return;
    // See MIN_CACHEABLE_TIER's own doc comment: tier 0 (dist<300m, right
    // around the camera) deliberately falls back to the live per-pixel
    // blend -- caching it exposed a real, inherent page-boundary artifact
    // (independently-baked neighbouring pages landing on uncorrelated
    // colours) exactly where a player's eye is most sensitive to it.
    if (tier < MIN_CACHEABLE_TIER) return;
    // See MAX_CACHEABLE_TIER's own doc comment: only the editor's
    // unbounded wide-overview viewport ever reaches this (the game's own
    // visibility cull never asks for a tier this coarse) -- without this,
    // a high-altitude editor camera thrashes the 512-slot budget across
    // however many far tiles happen to be in frame.
    if (tier > MAX_CACHEABLE_TIER) return;
    int subdiv = SubdivFactor(tier);
    int span   = FINE_SUBDIV / subdiv;  // fine cells per sub-page edge
    int base_fine_ix = patch_ix * FINE_SUBDIV;
    int base_fine_iz = patch_iz * FINE_SUBDIV;
    for (int sy = 0; sy < subdiv; ++sy) {
        for (int sx = 0; sx < subdiv; ++sx) {
            RequestSubtile(base_fine_ix + sx * span, base_fine_iz + sy * span, span, tier);
        }
    }
}

void TerrainVtPageCache::RequestSubtile(int fine_ix, int fine_iz, int span, int tier) {
    if (fine_ix < 0 || fine_iz < 0 || fine_ix >= INDIR_SIZE || fine_iz >= INDIR_SIZE) return;

    int existing = FindSlot(fine_ix, fine_iz);
    if (existing >= 0 && slots_[existing].span == span) {
        slots_[existing].last_touch = ++touch_counter_;  // resident -- bump LRU recency regardless of tier match
        if (slots_[existing].tier == tier) return;  // same content -- nothing else to do
        // Different tier, SAME span, for an already-resident fine corner:
        // reuse the SAME slot (indirection already points here, exactly
        // and only here, no re-upload needed) and queue a re-fill.
        if (fill_queue_count_ >= MAX_FILLS_PER_FRAME) return;
        slots_[existing].tier = tier;
        fill_queue_[fill_queue_count_++] = FillRequest{fine_ix, fine_iz, span, tier, existing};
        return;
    }

    if (fill_queue_count_ >= MAX_FILLS_PER_FRAME) return;  // this frame's fill budget exhausted
    if (invalidate_queue_count_ >= MAX_FILLS_PER_FRAME) return;  // matching cap -- an eviction always needs room in both queues

    if (existing >= 0) {
        // Live, reproduced regression (this session): a SPAN change at the
        // SAME fine corner (this patch's assigned tier crossed a distance
        // threshold -- happens even for a stationary camera, ordinary
        // float noise in TerrainPatchGrid::UpdateLOD's distance math flips
        // (int)p.lod across an integer boundary practically every frame
        // for any patch sitting near one) can NOT reuse the slot in place
        // the way a same-span tier change can: the OLD span's footprint
        // may cover MORE (or fewer) fine cells than the new one. Reusing
        // the slot in place while only re-uploading the NEW, narrower/
        // wider region left the fine cells the OLD span ALSO covered
        // silently pointing at a slot whose content now belongs to a
        // completely different, unrelated request -- neighbouring screen
        // pixels showing scrambled, uncorrelated colours (a checkerboard/
        // noise pattern, confirmed live via a RenderDoc capture at the
        // game's real spawn point). Fix: invalidate the OLD span's full
        // footprint first and fall through to the normal alloc-a-fresh-
        // slot path below, exactly as if this fine corner had never been
        // resident at all.
        invalidate_queue_[invalidate_queue_count_++] =
            InvalidateRequest{slots_[existing].fine_ix, slots_[existing].fine_iz, slots_[existing].span};
        slots_[existing].used = false;
        --resident_count_;
    }

    int slot = AllocSlot();
    bool was_used = slots_[slot].used;
    if (was_used) {
        // Evicting a DIFFERENT location's slot -- its indirection region
        // must be invalidated in the SAME flush, or the indirection
        // texture keeps pointing a stale fine-grid region at a slot that
        // no longer holds that location's content.
        invalidate_queue_[invalidate_queue_count_++] =
            InvalidateRequest{slots_[slot].fine_ix, slots_[slot].fine_iz, slots_[slot].span};
    } else {
        ++resident_count_;
    }

    SlotInfo si; si.fine_ix = fine_ix; si.fine_iz = fine_iz; si.span = span;
    si.tier = tier; si.used = true; si.last_touch = ++touch_counter_;
    slots_[slot] = si;
    fill_queue_[fill_queue_count_++] = FillRequest{fine_ix, fine_iz, span, tier, slot};
}

void TerrainVtPageCache::UploadIndirectionRegion(SDL_GPUDevice* dev, SDL_GPUCopyPass* cp,
                                                  int fine_ix, int fine_iz, int span, uint32_t value) {
    uint32_t vals[FINE_SUBDIV * FINE_SUBDIV];
    int count = span * span;
    for (int i = 0; i < count; ++i) vals[i] = value;

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size  = (Uint32)(count * (int)sizeof(uint32_t));
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    if (!tb) return;
    void* map = GpuMapTransfer(tb, false);
    if (map) memcpy(map, vals, tbi.size);
    GpuUnmapTransfer(tb);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.pixels_per_row  = (Uint32)span;
    src.rows_per_layer  = (Uint32)span;
    SDL_GPUTextureRegion dst{};
    dst.texture = indir_tex_.SDLTexture();
    dst.x = (Uint32)fine_ix; dst.y = (Uint32)fine_iz;
    dst.w = (Uint32)span; dst.h = (Uint32)span; dst.d = 1;
    // FromRaw: cp is the caller's already-open pass (FlushFillQueue), shared
    // across many Upload* calls in one loop for batching -- must NOT open
    // its own pass here, or the batching silently degrades to N passes.
    GpuCopyPass::FromRaw(cp, nullptr).UploadTexture(src, dst, false);
    // Transfer buffer is small and one-shot -- release immediately, same
    // as rd_texture.cpp's pattern (SDL_GPU tracks the copy internally,
    // safe to release right after recording the upload command).
    SDL_ReleaseGPUTransferBuffer(dev, tb);
}

void TerrainVtPageCache::UploadPageMeta(SDL_GPUDevice* dev, SDL_GPUCopyPass* cp, int slot,
                                         float origin_x, float origin_z, float world_size) {
    float meta[4] = { origin_x, origin_z, world_size, 0.f };

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size  = sizeof(meta);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    if (!tb) return;
    void* map = GpuMapTransfer(tb, false);
    if (map) memcpy(map, meta, sizeof(meta));
    GpuUnmapTransfer(tb);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    SDL_GPUBufferRegion dst{};
    dst.buffer = page_meta_buf_.SDLBuffer();
    dst.offset = (Uint32)(slot * (int)sizeof(meta));
    dst.size   = (Uint32)sizeof(meta);
    // FromRaw: same shared-pass batching contract as UploadIndirectionRegion.
    GpuCopyPass::FromRaw(cp, nullptr).UploadBuffer(src, dst, false);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
}

void TerrainVtPageCache::FlushFillQueue(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd,
                                         const TerrainWorldHeightmap& hmap, const TerrainRenderer& ground) {
    if (!ready_ || fill_queue_count_ == 0) { fill_queue_count_ = 0; return; }

    // 1) Upload this frame's new indirection regions + page-meta entries,
    // AND invalidate any evicted location's OLD region back to
    // kNotResident first (order matters only in the pathological case both
    // queues reference the exact same fine cell in the same frame, which
    // can't happen here: an invalidate is only ever queued for a DIFFERENT
    // location than the one being newly written, see RequestPage's
    // eviction branch).
    {
        GpuCopyPass cpw;
        cpw.Begin(cmd);
        SDL_GPUCopyPass* cp = cpw.SDLPass();
        if (cp) {
            for (int i = 0; i < invalidate_queue_count_; ++i) {
                const InvalidateRequest& r = invalidate_queue_[i];
                UploadIndirectionRegion(dev, cp, r.fine_ix, r.fine_iz, r.span, kNotResident);
            }
            for (int i = 0; i < fill_queue_count_; ++i) {
                const FillRequest& r = fill_queue_[i];
                UploadIndirectionRegion(dev, cp, r.fine_ix, r.fine_iz, r.span, (uint32_t)r.slot);
                UploadPageMeta(dev, cp, r.slot,
                               (float)r.fine_ix * FineCell(), (float)r.fine_iz * FineCell(),
                               (float)r.span * FineCell());
            }
        }
        cpw.End();
    }
    invalidate_queue_count_ = 0;

    // 2) Dispatch one page-fill compute invocation per queued request, each
    // in its OWN compute pass (Begin/End per dispatch, all still within
    // this ONE command buffer -- pushing new GpuPushComputeUniforms
    // between multiple dispatches inside a SINGLE compute pass works fine
    // once the real bug below was fixed; re-opening the pass per dispatch
    // is just the simplest way to guarantee a fresh, unambiguous binding
    // state each time, at the cost of re-binding the pipeline/samplers/SSBO
    // each iteration -- acceptable at this Phase 1 cadence, <=
    // MAX_FILLS_PER_FRAME=16). The REAL bug that made every dispatch
    // appear to write the same single page (see PageFillUBO's own doc
    // comment) was a std140 layout mismatch between this C++ struct and
    // the GLSL uniform block -- not a command-buffer/pass ordering issue;
    // confirmed via a live debug-color dispatch AND via fully separating
    // every dispatch into its own command buffer with a fence-wait, which
    // changed nothing until the UBO layout itself was fixed.
    //
    // Uses GpuComputePass via StorageBindings.rw_textures (M1 HAL-closure
    // follow-up: Begin used to hardcode storage-texture bindings to
    // nullptr,0, forcing this and SSAOSystem::Dispatch/the normal-bake
    // pass in terrain_world_heightmap.cpp to bypass the wrapper with raw
    // SDL -- all 3 real sites now go through GpuComputePass).
    const float world_origin  = hmap.WorldExtent() * 0.5f;  // overlay UV convention -- see PageFillUBO doc comment
    const float overlay_to_uv = 1.0f / hmap.WorldExtent();

    for (int i = 0; i < fill_queue_count_; ++i) {
        const FillRequest& r = fill_queue_[i];

        GpuComputePass::StorageBindings sb;
        sb.cmd = cmd;
        sb.rw_textures[0] = { atlas_tex_.SDLTexture(), false };
        sb.num_rw_textures = 1;

        GpuComputePass pass;
        pass.Begin(&fill_pipeline_, sb);
        if (!pass.SDLPass()) {
            MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] SDL_BeginGPUComputePass failed: %s", SDL_GetError());
            continue;
        }

        // set=0: 6 samplers -- heightTex (0), the 4 shared ground samplers
        // (1..4), zoneGroundLayersTex (5, texture not SSBO since
        // 2026-08-09) -- matching terrain_page_fill.comp's declared binding
        // order exactly. No storage buffers left in this set.
        SDL_GPUTextureSamplerBinding samplers[6];
        samplers[0] = { hmap.Texture(), hmap.Sampler() };
        SDL_GPUTextureSamplerBinding ground_bindings[4];
        ground.GetSharedGroundSamplers(ground_bindings);
        for (int gi = 0; gi < 4; ++gi) samplers[1 + gi] = ground_bindings[gi];
        samplers[5] = { ground.ZoneGroundLayersTexture(), ground.ZoneGroundLayersSampler() };
        pass.BindSamplers(0, samplers, 6);

        PageFillUBO ubo{};
        ubo.world_params[0] = world_origin; ubo.world_params[1] = world_origin;
        ubo.world_params[2] = overlay_to_uv;
        // terrain-vt clipmap fix: world_params.w carries the ORIGINAL
        // TerrainPatchGrid patch_size_ (300m), NOT this sub-page's (possibly
        // smaller) world_size below -- terrain_page_fill.comp's distForLod
        // represents a REAL camera distance for this tier (patch_size_ *
        // 2^tier, matching TerrainPatchGrid::UpdateLOD's own formula) and
        // must stay tied to the geometry grid's patch size regardless of how
        // finely this cache chooses to subdivide its OWN texture footprint.
        ubo.world_params[3] = patch_size_;
        ubo.hmap_world[0] = hmap.WorldExtent();
        ubo.hmap_world[1] = hmap.HeightMin();
        ubo.hmap_world[2] = hmap.HeightMax();
        ubo.hmap_world[3] = (float)hmap.Resolution();
        ubo.page_world[0] = (float)r.fine_ix * FineCell();
        ubo.page_world[1] = (float)r.fine_iz * FineCell();
        ubo.page_world[2] = (float)r.span * FineCell();
        ubo.page_world[3] = (float)r.tier;
        int slot_x = (r.slot % SLOTS_X) * PAGE_TEXELS;
        int slot_y = (r.slot / SLOTS_X) * PAGE_TEXELS;
        ubo.slot_offset[0] = slot_x; ubo.slot_offset[1] = slot_y;
        ubo.slot_offset[2] = 0; ubo.slot_offset[3] = 0;

        pass.PushUniforms(0, &ubo, sizeof(ubo));

        const uint32_t groups = (uint32_t)(PAGE_TEXELS / 8);  // 128/8 = 16
        pass.Dispatch(groups, groups, 1);

        pass.End();
    }
    fill_queue_count_ = 0;
}

bool TerrainVtPageCache::DebugDumpAtlas(SDL_GPUDevice* dev, const char* out_png_path) {
    if (!ready_ || !atlas_tex_.SDLTexture()) return false;
    uint32_t w = (uint32_t)(SLOTS_X * PAGE_TEXELS), h = (uint32_t)(SLOTS_Y * PAGE_TEXELS);
    uint32_t download_size = w * h * 4;

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbi.size  = download_size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    if (!tb) {
        MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] DebugDumpAtlas: transfer buffer create failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUCommandBuffer* cmd = md::GpuDevice::Get().AcquireCommandBuffer();
    GpuCopyPass cp;
    cp.Begin(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = atlas_tex_.SDLTexture();
    src.w = w; src.h = h; src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    dst.pixels_per_row  = w;
    dst.rows_per_layer  = h;
    cp.DownloadTexture(src, dst);
    cp.End();

    SDL_GPUFence* fence = md::GpuDevice::Get().SubmitAndAcquireFence(cmd);
    if (!fence) {
        MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] DebugDumpAtlas: submit failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        return false;
    }
    bool waited = md::GpuDevice::Get().WaitForFence(fence);
    md::GpuDevice::Get().ReleaseFence(fence);
    if (!waited) {
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        return false;
    }

    void* mapped = GpuMapTransfer(tb, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        return false;
    }
    int ok = stbi_write_png(out_png_path, (int)w, (int)h, 4, mapped, (int)(w * 4));
    GpuUnmapTransfer(tb);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    if (!ok) {
        MD_LOG(MD_LOG_WARNING, "[TerrainVtPageCache] DebugDumpAtlas: stbi_write_png failed: %s", out_png_path);
        return false;
    }
    MD_LOG(MD_LOG_INFO, "[TerrainVtPageCache] atlas dumped: %ux%u -> %s (%d pages resident)",
           w, h, out_png_path, resident_count_);
    return true;
}
#endif // MD_SDL_GPU
