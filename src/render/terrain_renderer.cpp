#include <monkey_dust/render/terrain_renderer.h>
#include <monkey_dust/world/biome_def.h>
#include <monkey_dust/tools/graphics_settings.h>
#include <monkey_dust/render/render_quality.h>
#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_device.h>
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

    // Per-zone (64x64=4096) ground-layer lookup — see UploadZoneGroundLayers.
    // 9 uint32 per zone (was 8): [0..5] base,slope,cliff,grass,dirt,road
    // GroundTexLayer indices; [6..7] real per-biome cliff UV tiling scale
    // (FCS "tiling X/Y 2", confirmed against terrainfp4.hlsl), bit-cast float
    // (uintBitsToFloat in the shader); [8] real per-biome brightness_fix
    // (FCS "brightness fix", terrainfp4.hlsl:212), also bit-cast float --
    // reuses this SSBO/binding rather than a second one. Flat index =
    // zone_idx*9 + slot. Allocated empty here; populated once by the caller
    // (World3D editor's synthesis-mesh init).
    zone_layers_ssbo_.Init(64 * 64 * 9 * (int)sizeof(uint32_t));
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

void TerrainRenderer::Shutdown() {
    tex_colour_.Shutdown();
    tex_ground_array_.Shutdown();
    tex_ground_nml_array_.Shutdown();
    tex_ground_baked_.Shutdown();
    ground_baked_ready_ = false;
    tex_biome_blend_.Shutdown();
    biome_blend_ready_ = false;
    tex_loaded_         = false;
    ground_array_ready_ = false;
    zone_layers_ssbo_.Shutdown();

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        if (fallback_sampler_)       SDL_ReleaseGPUSampler(dev, fallback_sampler_);
        if (fallback_tex_)           SDL_ReleaseGPUTexture(dev, fallback_tex_);
        if (fallback_mask_sampler_)  SDL_ReleaseGPUSampler(dev, fallback_mask_sampler_);
        if (fallback_mask_tex_)      SDL_ReleaseGPUTexture(dev, fallback_mask_tex_);
        if (fallback_blend_sampler_) SDL_ReleaseGPUSampler(dev, fallback_blend_sampler_);
        if (fallback_blend_tex_)     SDL_ReleaseGPUTexture(dev, fallback_blend_tex_);
    }
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
void TerrainRenderer::FillSamplerBindings(SDL_GPUTextureSamplerBinding out[4]) const
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
}

void TerrainRenderer::GetSharedGroundSamplers(SDL_GPUTextureSamplerBinding out[3]) const {
    SDL_GPUTextureSamplerBinding all[4];
    FillSamplerBindings(all);
    out[0] = all[0];  // tex_colour
    out[1] = all[1];  // tex_ground_array
    out[2] = all[2];  // tex_ground_baked
}
#endif

void TerrainRenderer::UploadZoneGroundLayers(const uint32_t* data, int count_uints) {
#ifdef MD_SDL_GPU
    if (count_uints != 64 * 64 * 9) {
        fprintf(stderr, "[TerrainRenderer] UploadZoneGroundLayers: expected %d uints, got %d — skipped\n",
                64 * 64 * 9, count_uints);
        return;
    }
    zone_layers_ssbo_.Upload(data, count_uints * (int)sizeof(uint32_t));
#else
    (void)data; (void)count_uints;
#endif
}
