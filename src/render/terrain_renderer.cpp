#include <monkey_dust/render/terrain_renderer.h>
#include <monkey_dust/world/biome_def.h>
#include <monkey_dust/tools/graphics_settings.h>
#include <monkey_dust/render/render_quality.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/render/gpu_hal.h>
#endif

bool TerrainRenderer::Init() {
#ifdef MD_SDL_GPU
    // Create 1×1 white fallback texture for slots where InitTextures was not
    // called or an individual texture failed to load.
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::REPEAT;
    sd.wrap_t     = GpuSamplerDesc::Wrap::REPEAT;
    sd.gen_mipmap = false;
    sd.flip_v     = false;
    uint8_t white[4] = { 255, 255, 255, 255 };
    GpuTexture fb;
    if (fb.InitFromMemory(white, 1, 1, sd)) {
        fallback_tex_     = fb.TakeSDLTexture();
        fallback_sampler_ = fb.TakeSDLSampler();
    }
    // Overlay-mask fallback: all-zero RGBA — no grass/dirt/road painted
    // anywhere, so base/slope/cliff blend alone if the mask fails to load
    // (safe default, not a wrong-looking one). Only used by the editor's
    // zone-lookup draw path (InitOverlayMask) — was created in InitPOM
    // before the 2026-07-19 rewrite, moved here since InitPOM is gone.
    uint8_t mask_neutral[4] = { 0, 0, 0, 0 };
    GpuTexture fbm;
    if (fbm.InitFromMemory(mask_neutral, 1, 1, sd)) {
        fallback_mask_tex_     = fbm.TakeSDLTexture();
        fallback_mask_sampler_ = fbm.TakeSDLSampler();
    }
    // Biome-blend fallback: A=0 -- no cross-fade, pure current-zone biome.
    // Same zone-lookup-path-only scope as the mask fallback above.
    uint8_t blend_neutral[4] = { 0, 0, 0, 0 };
    GpuTexture fbb;
    if (fbb.InitFromMemory(blend_neutral, 1, 1, sd)) {
        fallback_blend_tex_     = fbb.TakeSDLTexture();
        fallback_blend_sampler_ = fbb.TakeSDLSampler();
    }

    // Per-zone (64x64=4096) ground-layer lookup texture -- see
    // UploadZoneGroundLayers. 9 uint32 per zone: [0..5] base,slope,cliff,
    // grass,dirt,road GroundTexLayer indices; [6..7] real per-biome cliff UV
    // tiling scale (FCS "tiling X/Y 2", confirmed against terrainfp4.hlsl),
    // bit-cast float (uintBitsToFloat in the shader); [8] real per-biome
    // brightness_fix (FCS "brightness fix", terrainfp4.hlsl:212), also
    // bit-cast float. R32G32B32A32_UINT, 192x64 (3 texels/zone x 4 channels
    // = 12 slots, 9 used) -- a texture, not an SSBO, as of 2026-08-09 (see
    // ZoneGroundLayersTexture's header doc comment for why). Allocated
    // empty here; populated once by the caller (World3D editor's
    // synthesis-mesh init, and SceneRender's game-side equivalent).
    {
        SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
        SDL_GPUTextureCreateInfo ti{};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT;
        ti.width                = 64 * 3;
        ti.height                = 64;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        zone_layers_tex_ = SDL_CreateGPUTexture(dev, &ti);

        SDL_GPUSamplerCreateInfo si{};
        si.min_filter     = SDL_GPU_FILTER_NEAREST;
        si.mag_filter     = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        zone_layers_sampler_ = SDL_CreateGPUSampler(dev, &si);
    }
#endif
    return true;
}

bool TerrainRenderer::InitGroundTextureArray()
{
#ifdef MD_SDL_GPU
    // Note: InitFromDDSArray ignores this sampler desc entirely and always
    // creates its own LINEAR_MIPMAP_LINEAR sampler from the DDS's own mip
    // chain (see gpu_hal_buffers.cpp) -- kept here only as the function's
    // required argument shape.
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::REPEAT;
    sd.wrap_t     = GpuSamplerDesc::Wrap::REPEAT;
    sd.gen_mipmap = false;
    const BiomeRegistry& biomes = BiomeRegistry::Get();
    const int tex_count = biomes.GroundTexCount();
    const char* dif_ptrs[BiomeRegistry::MAX_TEXTURES];
    const char* nml_ptrs[BiomeRegistry::MAX_TEXTURES];
    for (int i = 0; i < tex_count; ++i) {
        dif_ptrs[i] = biomes.GroundTexPath(i);
        nml_ptrs[i] = biomes.GroundNmlPath(i);
    }

    tex_ground_array_.Shutdown();
    if (!tex_ground_array_.InitFromDDSArray(dif_ptrs, tex_count, sd)) {
        fprintf(stderr, "[TerrainRenderer] ground texture array failed\n");
        ground_array_ready_ = false;
        return false;
    }
    tex_ground_nml_array_.Shutdown();
    if (!tex_ground_nml_array_.InitFromDDSArray(nml_ptrs, tex_count, sd)) {
        fprintf(stderr, "[TerrainRenderer] ground normal array failed — terrain lighting falls back to flat vertex normal\n");
        ground_array_ready_ = false;
        return false;
    }
    ground_array_ready_ = true;
    fprintf(stderr, "[TerrainRenderer] ground texture+normal arrays ready (%d layers)\n", tex_count);
    return true;
#else
    return false;
#endif
}

bool TerrainRenderer::InitGroundBaked(const char* path)
{
#ifdef MD_SDL_GPU
    // Offline-baked flat-ground colour (task #306, tools/md_bake_ground_
    // layers.py + tools/md_encode_ground_bake.py) -- replaces the old
    // live per-pixel base/slope/grass/dirt/road blend (GetSharedGroundSamplers'
    // 3rd slot used to be tex_overlay_mask_, the painted grass/dirt/road
    // mask itself; that live blend chain moved offline, so this slot now
    // holds the RESULT of that blend instead of one of its inputs).
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = true;
    sd.flip_v     = false;

    tex_ground_baked_.Shutdown();
    if (!tex_ground_baked_.InitFromFile(path, sd)) {
        fprintf(stderr, "[TerrainRenderer] ground baked texture failed: %s\n", path);
        ground_baked_ready_ = false;
        return false;
    }
    ground_baked_ready_ = true;
    fprintf(stdout, "[TerrainRenderer] ground baked texture loaded: %s\n", path);
    return true;
#else
    return false;
#endif
}

bool TerrainRenderer::InitBiomeBlend(const char* path)
{
#ifdef MD_SDL_GPU
    GpuSamplerDesc sd;
    // LINEAR safe: this file (private/md_gen_biome_blendmap.py, v6,
    // 2026-07-19) is now a direct 1:1 copy of the REAL Kenshi
    // data/newland/land/blendmap.png -- confirmed every channel is
    // strictly binary 0/255 (spot-checked at copy time), and R/G/B/A are 4
    // INDEPENDENT masks (not identical, 27%/25%/17%/24% nonzero) -- the
    // smooth ramp comes entirely from the GPU's own bilinear sampling of
    // this binary mask, not pre-blurred data, matching real Kenshi's own
    // mechanism (terrainfp4.hlsl).
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = false;
    sd.flip_v     = false;

    tex_biome_blend_.Shutdown();
    if (!tex_biome_blend_.InitFromFile(path, sd)) {
        fprintf(stderr, "[TerrainRenderer] biome blend map failed: %s\n", path);
        biome_blend_ready_ = false;
        return false;
    }
    biome_blend_ready_ = true;
    fprintf(stdout, "[TerrainRenderer] biome blend map loaded: %s\n", path);
    return true;
#else
    return false;
#endif
}

bool TerrainRenderer::InitOverlayMask(const char* path)
{
#ifdef MD_SDL_GPU
    GpuSamplerDesc sd;
    // Same rationale as InitBiomeBlend: LINEAR (not LINEAR_MIPMAP) — this is
    // a mask sampled at a live per-fragment UV close to the camera, not a
    // pre-blurred colour texture, and generating mips of a thin-line road
    // mask would fade it out at exactly the mid distances the detail-restore
    // layer covers.
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = false;
    sd.flip_v     = false;

    tex_overlay_mask_.Shutdown();

    // Fast path: the same pre-baked .raw sidecar clutter_gen.cpp's
    // s_load_grass_density() already uses (tools/md_stitch_overlay_mask.py)
    // -- a plain fread instead of stb_image's ~760ms+ single-threaded PNG
    // DEFLATE decode of this 4096x4096 image (measured here: InitFromFile's
    // PNG path alone added ~1.9s to game startup). Header = 2x little-endian
    // uint32 (width,height) then raw RGBA8 bytes; falls back to the .png if
    // the sidecar is missing/stale (it's gitignored, regenerate-only).
    uint32_t raw_len = 0;
    char* raw = md::fs_read_alloc("game/data/textures/md_overlay_mask.raw", &raw_len);
    if (raw && raw_len > 8) {
        uint32_t w, h;
        memcpy(&w, raw,     4);
        memcpy(&h, raw + 4, 4);
        bool ok = (uint64_t)raw_len - 8 == (uint64_t)w * h * 4
                  && tex_overlay_mask_.InitFromMemory(reinterpret_cast<const uint8_t*>(raw + 8),
                                                       (int)w, (int)h, sd);
        md::fs_free(raw);
        if (ok) {
            overlay_mask_ready_ = true;
            fprintf(stdout, "[TerrainRenderer] overlay mask loaded (raw sidecar): %ux%u\n", w, h);
            return true;
        }
    }

    if (!tex_overlay_mask_.InitFromFile(path, sd)) {
        fprintf(stderr, "[TerrainRenderer] overlay mask failed: %s\n", path);
        overlay_mask_ready_ = false;
        return false;
    }
    overlay_mask_ready_ = true;
    fprintf(stdout, "[TerrainRenderer] overlay mask loaded: %s\n", path);
    return true;
#else
    return false;
#endif
}

void TerrainRenderer::Shutdown() {
    tex_colour_.Shutdown();
    tex_ground_array_.Shutdown();
    tex_ground_nml_array_.Shutdown();
    tex_ground_baked_.Shutdown();
    ground_baked_ready_ = false;
    tex_biome_blend_.Shutdown();
    biome_blend_ready_ = false;
    tex_overlay_mask_.Shutdown();
    overlay_mask_ready_ = false;
    tex_loaded_         = false;
    ground_array_ready_ = false;

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        if (fallback_sampler_)       SDL_ReleaseGPUSampler(dev, fallback_sampler_);
        if (fallback_tex_)           SDL_ReleaseGPUTexture(dev, fallback_tex_);
        if (fallback_mask_sampler_)  SDL_ReleaseGPUSampler(dev, fallback_mask_sampler_);
        if (fallback_mask_tex_)      SDL_ReleaseGPUTexture(dev, fallback_mask_tex_);
        if (fallback_blend_sampler_) SDL_ReleaseGPUSampler(dev, fallback_blend_sampler_);
        if (fallback_blend_tex_)     SDL_ReleaseGPUTexture(dev, fallback_blend_tex_);
        if (zone_layers_sampler_)    SDL_ReleaseGPUSampler(dev, zone_layers_sampler_);
        if (zone_layers_tex_)        SDL_ReleaseGPUTexture(dev, zone_layers_tex_);
    }
    zone_layers_tex_     = nullptr;
    zone_layers_sampler_ = nullptr;
    fallback_tex_            = nullptr;
    fallback_sampler_        = nullptr;
    fallback_mask_tex_       = nullptr;
    fallback_mask_sampler_   = nullptr;
    fallback_blend_tex_      = nullptr;
    fallback_blend_sampler_  = nullptr;
#endif
}

bool TerrainRenderer::IsReady() const {
    // No own draw pipeline anymore (see class doc comment) -- readiness now
    // just means the shared ground textures Granite depends on are loaded.
    return ground_array_ready_ && ground_baked_ready_;
}

bool TerrainRenderer::InitKenshiOverlay(const char* path)
{
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = true;
    sd.flip_v     = false;

    tex_colour_.Shutdown();
    if (!tex_colour_.InitFromFile(path, sd)) {
        fprintf(stderr, "[TerrainRenderer] kenshi overlay failed: %s\n", path);
        return false;
    }
    tex_loaded_ = true;
    fprintf(stdout, "[TerrainRenderer] kenshi overlay loaded: %s\n", path);
    return true;
}


#ifdef MD_SDL_GPU
void TerrainRenderer::FillSamplerBindings(SDL_GPUTextureSamplerBinding out[5]) const
{
    bool valid = tex_loaded_ && tex_colour_.Valid()
                 && tex_colour_.SDLTexture() && tex_colour_.SDLSampler();
    out[0].texture = valid ? tex_colour_.SDLTexture() : fallback_tex_;
    out[0].sampler = valid ? tex_colour_.SDLSampler() : fallback_sampler_;
    // b1: per-biome DDS ground array — the actual per-vertex-indexed ground
    // textures (see terrain_gen.cpp's s_ground_pick / TerrainVertex).
    bool ga = ground_array_ready_ && tex_ground_array_.Valid()
              && tex_ground_array_.SDLTexture() && tex_ground_array_.SDLSampler();
    out[1].texture = ga ? tex_ground_array_.SDLTexture() : nullptr;
    out[1].sampler = ga ? tex_ground_array_.SDLSampler() : nullptr;
    // b2: offline-baked flat-ground colour (task #306) — TerrainPatchRenderer's
    // flat-ground path only (was the painted grass/dirt/road mask before
    // that blend moved offline; the per-chunk near/mid path never used
    // this slot, resolving ground layers at generation time instead).
    bool mv = ground_baked_ready_ && tex_ground_baked_.Valid()
              && tex_ground_baked_.SDLTexture() && tex_ground_baked_.SDLSampler();
    out[2].texture = mv ? tex_ground_baked_.SDLTexture() : fallback_mask_tex_;
    out[2].sampler = mv ? tex_ground_baked_.SDLSampler() : fallback_mask_sampler_;
    // b3: procedural biome-crossfade blend map — loaded, currently unconsumed
    // (GetSharedGroundSamplers only exposes 3 of these 4 slots to Granite).
    bool bv = biome_blend_ready_ && tex_biome_blend_.Valid()
              && tex_biome_blend_.SDLTexture() && tex_biome_blend_.SDLSampler();
    out[3].texture = bv ? tex_biome_blend_.SDLTexture() : fallback_blend_tex_;
    out[3].sampler = bv ? tex_biome_blend_.SDLSampler() : fallback_blend_sampler_;
    // b4: grass/dirt/road paint mask (task terrain-detail-erases-road,
    // 2026-08-01) — TerrainPatchRenderer's close-range detail-restore layer
    // only; reuses fallback_mask_tex_/sampler_ (all-zero — no grass/dirt/
    // road painted anywhere, safe default) since it's the same neutral
    // shape this slot already had before InitPOM's removal.
    bool om = overlay_mask_ready_ && tex_overlay_mask_.Valid()
              && tex_overlay_mask_.SDLTexture() && tex_overlay_mask_.SDLSampler();
    out[4].texture = om ? tex_overlay_mask_.SDLTexture() : fallback_mask_tex_;
    out[4].sampler = om ? tex_overlay_mask_.SDLSampler() : fallback_mask_sampler_;
}

void TerrainRenderer::GetSharedGroundSamplers(SDL_GPUTextureSamplerBinding out[4]) const {
    SDL_GPUTextureSamplerBinding all[5];
    FillSamplerBindings(all);
    out[0] = all[0];  // tex_colour
    out[1] = all[1];  // tex_ground_array
    out[2] = all[2];  // tex_ground_baked
    out[3] = all[4];  // tex_overlay_mask
}
#endif

void TerrainRenderer::UploadZoneGroundLayers(const uint32_t* data, int count_uints) {
#ifdef MD_SDL_GPU
    if (count_uints != 64 * 64 * 9) {
        fprintf(stderr, "[TerrainRenderer] UploadZoneGroundLayers: expected %d uints, got %d — skipped\n",
                64 * 64 * 9, count_uints);
        return;
    }
    if (!zone_layers_tex_) return;
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (!dev) return;

    // Repack the caller's flat zone_idx*9+slot layout into the texture's
    // 3-texels-per-zone x 4-channels layout (12 slots available, 9 used --
    // see ZoneGroundLayersTexture's header doc comment).
    const int W = 64 * 3, H = 64;
    std::vector<uint32_t> packed((size_t)W * H * 4, 0u);
    for (int zy = 0; zy < 64; ++zy) {
        for (int zx = 0; zx < 64; ++zx) {
            int zone_idx = zy * 64 + zx;
            for (int slot = 0; slot < 9; ++slot) {
                int t = slot / 4, c = slot % 4;
                size_t texel_idx = (size_t)zy * W + (size_t)(zx * 3 + t);
                packed[texel_idx * 4 + (size_t)c] = data[(size_t)zone_idx * 9 + (size_t)slot];
            }
        }
    }

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size  = (Uint32)(packed.size() * sizeof(uint32_t));
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    if (!tb) return;
    void* map = GpuMapTransfer(tb, false);
    if (map) memcpy(map, packed.data(), tbi.size);
    GpuUnmapTransfer(tb);

    SDL_GPUCommandBuffer* cmd = md::GpuDevice::Get().AcquireCommandBuffer();
    GpuCopyPass cp;
    cp.Begin(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.pixels_per_row  = (Uint32)W;
    src.rows_per_layer  = (Uint32)H;
    SDL_GPUTextureRegion dst{};
    dst.texture = zone_layers_tex_;
    dst.w = (Uint32)W; dst.h = (Uint32)H; dst.d = 1;
    cp.UploadTexture(src, dst, false);
    cp.End();
    md::GpuDevice::Get().Submit(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
#else
    (void)data; (void)count_uints;
#endif
}
