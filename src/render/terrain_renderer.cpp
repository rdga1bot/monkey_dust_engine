#include <monkey_dust/render/terrain_renderer.h>
#include <monkey_dust/world/biome_def.h>
#include <monkey_dust/world/clutter_gen.h>
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
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_forward.vert";
    pd.frag_path = "shaders/terrain_forward.frag";

    // TerrainVertex layout (stride=52) — this is now the ONLY terrain
    // pipeline (2026-07-19: no more POM/forward split, see
    // shaders/slang/terrain_forward.slang's file header):
    //   loc 0: vec3 pos     offset  0
    //   loc 1: vec3 normal  offset 12
    //   loc 2: vec2 uv      offset 24
    //   loc 3: vec4 ground  offset 32  (ground_id, ground_id2, blend_alpha, _reserved — was aSplat)
    //   loc 4: float morphY offset 48  (task #182g, 2026-07-19: WIRED UP — was declared in
    //          TerrainVertex/stride math but never added to the pipeline's own vertex input
    //          layout, so the shader could never actually read it regardless of its own code —
    //          see terrain_forward.slang's aMorphY doc comment for the other half of this fix)
    pd.layout.count      = 5;
    pd.layout.stride     = 52;  // pos(12)+norm(12)+uv(8)+ground(16)+morph_y(4)
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };  // aPos
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };  // aNormal
    pd.layout.attribs[2] = { 2, 24, GpuAttribFmt::F2 };  // aUV
    pd.layout.attribs[3] = { 3, 32, GpuAttribFmt::F4 };  // aGround
    pd.layout.attribs[4] = { 4, 48, GpuAttribFmt::F1 };  // aMorphY

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;  // SDL_GPU Vulkan Y-flip inverts winding vs OpenGL convention
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;  // slot 0: TerrainVertUBO
    pd.frag_uniform_bufs = 1;  // slot 0: TerrainFragUBO
    // b5 (baked per-chunk albedo) removed 2026-07-19 — per-vertex ground
    // selection replaced BakeAlbedo entirely, see FillSamplerBindings.
    pd.frag_samplers     = 5;  // b0=kenshi colour overlay, b1=ground DDS array, b2=detail tint, b3=overlay mask, b4=biome blend
    // b5,b6 (right after the 5 samplers, SDL_GPU set=2 contiguous layout):
    // b5=per-zone ground-layer lookup (zone_layers_ssbo_), used only when
    // TerrainFragUBO.use_zone_lookup.x>0.5 (synthesis/compact-LOD2
    // background draws — see SetBatchZoneLookup). b6=per-chunk baked
    // full-res steepness (task #182, 2026-07-19 — see TerrainChunk::
    // steepness_ssbo's doc comment), read by fsMain's per-chunk (non-
    // lookup) branch instead of interpolating N.y across the LOD triangle.
    // b7=comboAlternates (task #182c/d, 2026-07-19) — small GLOBAL (not
    // per-chunk) table of the real Kenshi biome-crossfade alternates, see
    // combo_alt_ssbo_'s doc comment (terrain_renderer.h).
    pd.frag_storage_bufs = 3;

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
#endif

    if (!pipeline_.Create(pd)) return false;

#ifdef MD_SDL_GPU
    // Shared LOD IBOs — built once, reused by BeginRawBatch for all chunks
    // (editor's synthesis/compact-LOD whole-world path). Sized from
    // TERRAIN_LOD_IDX[0] (largest LOD level), not a hardcoded literal —
    // same overflow risk as terrain_upload.cpp's s_lod_tmp.
    // Task #182 (2026-07-19): was a plain uniform decimation, duplicated
    // from terrain_upload.cpp's build_lod_ibo — now calls the SAME shared
    // BuildLodIboStitched (terrain_chunk.h) so this path gets the
    // T-junction fix too, instead of silently keeping the old bug.
    static uint16_t s_tmp[TERRAIN_LOD_IDX[0]];
    for (int li = 0; li < TERRAIN_LOD_LEVELS; ++li) {
        int step = TERRAIN_LOD_STEPS[li];
        BuildLodIboStitched(s_tmp, step);
        lod_ibo_shared_[li].Init(0x8893u, s_tmp, sizeof(uint16_t)*TERRAIN_LOD_IDX[li]);
    }

    // Per-zone (64x64=4096) ground-layer lookup — see UploadZoneGroundLayers.
    // 6 uint32 per zone (base,slope,cliff,grass,dirt,road GroundTexLayer
    // indices), flat index = zone_idx*6 + layer. Allocated empty here;
    // populated once by the caller (World3D editor's synthesis-mesh init).
    zone_layers_ssbo_.Init(64 * 64 * 6 * (int)sizeof(uint32_t));

    // Task #182: all-zero (flat) fallback for TerrainChunk::steepness_ssbo —
    // see FillStorageBindings/fallback_steepness_ssbo_'s doc comment.
    {
        static float s_zero_steep[TERRAIN_VERTS] = {};
        fallback_steepness_ssbo_.Init(GPU_TARGET_STORAGE, s_zero_steep, sizeof(s_zero_steep));
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

    // Task #182c/d (2026-07-19): resolve+upload the global biome-crossfade
    // alternates HERE — outside any render pass, safe for GpuStaticBuffer's
    // internal command-buffer+copy-pass upload (see combo_alt_ssbo_'s doc
    // comment, terrain_renderer.h, and FillStorageBindings's comment for
    // why this must NOT happen lazily mid-frame). BiomeRegistry is already
    // required to be loaded by this point (used above via biomes.*).
    {
        const BiomeDef& altR = biomes.ForColor(255, 0, 0);
        const BiomeDef& altG = biomes.ForColor(0, 255, 0);
        const BiomeDef& altB = biomes.ForColor(0, 0, 255);
        float combo_data[18] = {
            (float)altR.tex_base, (float)altR.tex_slope, (float)altR.tex_cliff,
            (float)altR.tex_grass,(float)altR.tex_dirt,  (float)altR.tex_road,
            (float)altG.tex_base, (float)altG.tex_slope, (float)altG.tex_cliff,
            (float)altG.tex_grass,(float)altG.tex_dirt,  (float)altG.tex_road,
            (float)altB.tex_base, (float)altB.tex_slope, (float)altB.tex_cliff,
            (float)altB.tex_grass,(float)altB.tex_dirt,  (float)altB.tex_road,
        };
        combo_alt_ssbo_.Shutdown();
        combo_alt_ssbo_.Init(GPU_TARGET_STORAGE, combo_data, sizeof(combo_data));
        combo_alt_ready_ = true;
    }
    return true;
#else
    return false;
#endif
}

bool TerrainRenderer::InitOverlayMask(const char* path)
{
#ifdef MD_SDL_GPU
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = true;
    sd.flip_v     = false;

    tex_overlay_mask_.Shutdown();

    // ClutterGen_LoadSources() (called at startup before any terrain/scene
    // init) already decoded this exact 4096x4096 PNG for CPU-side grass-
    // density sampling — reuse those pixels instead of a second stbi_load
    // of the same file (was doubling this texture's decode cost every
    // startup). Falls back to InitFromFile for callers that never went
    // through ClutterGen_LoadSources (e.g. editor tools).
    const uint8_t* px = nullptr;
    int pw = 0, ph = 0;
    bool ok = ClutterGen_GetGrassDensityRGBA(&px, &pw, &ph)
                  ? tex_overlay_mask_.InitFromMemory(px, pw, ph, sd)
                  : tex_overlay_mask_.InitFromFile(path, sd);
    if (!ok) {
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
    // mechanism (terrainfp4.hlsl). terrain_forward.slang combines all 4
    // channels via max() into one weight; the blend-target texture INDEX
    // still comes from TerrainChunk::blend_layers (a per-chunk UBO
    // constant, monkey_dust's own simplified stand-in for Kenshi's real
    // per-page diffuseMaps1/2/3 identity system, blendinfo.dat -- see
    // terrain_gen.cpp's ARCHITECTURE NOTE).
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

bool TerrainRenderer::InitDetailTexture(const char* path)
{
#ifdef MD_SDL_GPU
    if (!path || !path[0]) return false;
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::REPEAT;
    sd.wrap_t     = GpuSamplerDesc::Wrap::REPEAT;
    sd.gen_mipmap = true;
    sd.flip_v     = false;
    tex_detail_.Shutdown();
    if (!tex_detail_.InitFromFile(path, sd)) {
        fprintf(stderr, "[TerrainRenderer] detail texture '%s' failed — using neutral fallback\n", path);
        return false;
    }
    return true;
#else
    return false;
#endif
}

void TerrainRenderer::Shutdown() {
    tex_colour_.Shutdown();
    tex_ground_array_.Shutdown();
    tex_ground_nml_array_.Shutdown();
    tex_detail_.Shutdown();
    tex_overlay_mask_.Shutdown();
    overlay_mask_ready_ = false;
    tex_biome_blend_.Shutdown();
    biome_blend_ready_ = false;
    tex_loaded_         = false;
    ground_array_ready_ = false;
    zone_layers_ssbo_.Shutdown();
    fallback_steepness_ssbo_.Shutdown();
    combo_alt_ssbo_.Shutdown();
    combo_alt_ready_ = false;

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

    pipeline_.Destroy();
}

bool TerrainRenderer::IsReady() const {
#ifdef MD_SDL_GPU
    return pipeline_.SDLPipeline() != nullptr;
#else
    return false;
#endif
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
    // b2: generic detail tint (InitDetailTexture; fallback=white 1×1 → neutral blend)
    bool det = tex_detail_.Valid()
               && tex_detail_.SDLTexture() && tex_detail_.SDLSampler();
    out[2].texture = det ? tex_detail_.SDLTexture() : fallback_tex_;
    out[2].sampler = det ? tex_detail_.SDLSampler() : fallback_sampler_;
    // b3: painted grass/dirt/road mask — zone-lookup draw path only (the
    // per-chunk near/mid path resolves this at generation time instead).
    bool mv = overlay_mask_ready_ && tex_overlay_mask_.Valid()
              && tex_overlay_mask_.SDLTexture() && tex_overlay_mask_.SDLSampler();
    out[3].texture = mv ? tex_overlay_mask_.SDLTexture() : fallback_mask_tex_;
    out[3].sampler = mv ? tex_overlay_mask_.SDLSampler() : fallback_mask_sampler_;
    // b4: procedural biome-crossfade blend map — zone-lookup draw path only.
    bool bv = biome_blend_ready_ && tex_biome_blend_.Valid()
              && tex_biome_blend_.SDLTexture() && tex_biome_blend_.SDLSampler();
    out[4].texture = bv ? tex_biome_blend_.SDLTexture() : fallback_blend_tex_;
    out[4].sampler = bv ? tex_biome_blend_.SDLSampler() : fallback_blend_sampler_;
}

void TerrainRenderer::FillStorageBindings(SDL_GPUBuffer* out[3], const TerrainChunk* chunk)
{
    out[0] = zone_layers_ssbo_.SDLBuffer();
    out[1] = (chunk && chunk->steepness_ssbo.SDLBuffer())
                 ? chunk->steepness_ssbo.SDLBuffer()
                 : fallback_steepness_ssbo_.SDLBuffer();
    // combo_alt_ssbo_ is resolved+uploaded once in InitGroundTextureArray()
    // (safe: outside any render pass) — NOT lazily here. An earlier version
    // called GpuStaticBuffer::Init() from this function, which is invoked
    // WHILE the caller already has a render pass open on its own command
    // buffer; Init() internally acquires a SEPARATE command buffer and copy
    // pass with no fence/wait against the in-flight render pass, a real
    // race (confirmed by a user-reported ~3x FPS regression + visible
    // corruption — a solid-colour quad where a chunk should be — immediately
    // after this fix shipped). If combo_alt_ready_ is somehow still false
    // here (InitGroundTextureArray not yet called), fall back to the
    // all-zero fallback_steepness_ssbo_ buffer (same size class, safely
    // already-initialized) rather than risk another unsafe lazy Init().
    out[2] = combo_alt_ready_ ? combo_alt_ssbo_.SDLBuffer() : fallback_steepness_ssbo_.SDLBuffer();
}
#endif

void TerrainRenderer::DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                              const TerrainChunk& chunk,
                              const float* vp16,
                              const SunParams& sun,
                              float cam_x, float cam_y, float cam_z,
                              float world_origin_x,
                              float world_origin_z,
                              float world_to_uv,
                              int   lod,
                              float lod_blend,
                              float fog_density_override)
{
    if (!IsReady() || !chunk.loaded || !chunk.vbo.SDLBuffer() || !chunk.ibo.SDLBuffer()) return;
#ifdef MD_SDL_GPU
    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());
    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    int lod_clamped = (lod < 0) ? 0 : (lod > TERRAIN_LOD_LEVELS) ? TERRAIN_LOD_LEVELS : lod;
    const SDL_GPUBuffer* ibo_buf;
    uint32_t             idx_count;
    if (lod_clamped == 0 || !chunk.ibo_lod[0].SDLBuffer()) {
        ibo_buf   = chunk.ibo.SDLBuffer();
        idx_count = (uint32_t)TERRAIN_IDX;
    } else {
        ibo_buf   = chunk.ibo_lod[lod_clamped - 1].SDLBuffer();
        idx_count = (uint32_t)TERRAIN_LOD_IDX[lod_clamped - 1];
    }
    SDL_GPUBufferBinding ib { const_cast<SDL_GPUBuffer*>(ibo_buf), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    TerrainVertUBO vubo;
    memcpy(vubo.vp, vp16, 64);
    vubo.world_origin_x = world_origin_x;
    vubo.world_origin_z = world_origin_z;
    vubo.world_to_uv    = world_to_uv;
    vubo.lod_blend      = lod_blend;
    vubo.cam_pos_ws[0] = cam_x; vubo.cam_pos_ws[1] = cam_y;
    vubo.cam_pos_ws[2] = cam_z; vubo.cam_pos_ws[3] = 0.f;
    vubo.chunk_origin_x = chunk.center_x - CHUNK_SIZE * 0.5f;
    vubo.chunk_origin_z = chunk.center_z - CHUNK_SIZE * 0.5f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    const auto& fog = GraphicsSettings::Get();
    // Linear fog, tied to terrain_cr_m — see BeginRawBatch's comment.
    const float fog_far = (fog_density_override > 0.f) ? fog_density_override
                                                         : RenderQualityConfig::Get().terrain_cr_m;
    TerrainFragUBO fubo{};  // {} zero-inits use_zone_lookup — never set below (task #182 fix)
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0]= world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2]= world_to_uv;    fubo.world_params[3] = 0.f;
    fubo.ground_layers_a[0] = chunk.ground_layers[0]; fubo.ground_layers_a[1] = chunk.ground_layers[1];
    fubo.ground_layers_a[2] = chunk.ground_layers[2]; fubo.ground_layers_a[3] = chunk.ground_layers[3];
    fubo.ground_layers_b[0] = chunk.ground_layers[4]; fubo.ground_layers_b[1] = chunk.ground_layers[5];
    fubo.ground_layers_b[2] = fog_far; fubo.ground_layers_b[3] = 0.f;
    fubo.fog_color_near[0] = fog.fog_color[0]; fubo.fog_color_near[1] = fog.fog_color[1];
    fubo.fog_color_near[2] = fog.fog_color[2]; fubo.fog_color_near[3] = fog.fog_near_ratio * fog_far;
    fubo.blend_layers[0] = chunk.blend_layers[0]; fubo.blend_layers[1] = chunk.blend_layers[1];
    fubo.blend_layers[2] = chunk.blend_layers[2]; fubo.blend_layers[3] = chunk.blend_layers[3];
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[5];
    FillSamplerBindings(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 5);

    SDL_GPUBuffer* sbufs[3];
    FillStorageBindings(sbufs, &chunk);
    SDL_BindGPUFragmentStorageBuffers(rp, 0, sbufs, 3);

    SDL_DrawGPUIndexedPrimitives(rp, idx_count, 1, 0, 0, 0);

    // 2026-07-19: was LOD0-only ("distant chunks never need it") — wrong.
    // build_lod_ibo() decimates LOD1+ by sampling every step-th vertex
    // along a chunk's boundary too, so its boundary edge is a coarser
    // straight-line approximation of the real (fine) height profile —
    // exactly where an adjacent chunk at a DIFFERENT LOD tier (or one
    // still mid-stream/rebuilding) doesn't share the same boundary
    // vertices, a thin sliver of sky shows through (confirmed via
    // screenshots: small blue triangular gaps scattered across LOD1+
    // terrain, not just at the horizon silhouette). The skirt is always
    // built at LOD0 density (s_skirt_v, terrain_gen.cpp) regardless of
    // which LOD tier ends up drawn, so it does not perfectly hug a
    // decimated LOD1+ boundary either — but it drops SKIRT_DROP=2m below
    // the surface at every fine-grained boundary point, which covers the
    // gap for all but the steepest local relief. Net effect: strictly
    // fewer/smaller gaps than drawing no skirt at all. A per-LOD-tier
    // skirt (own decimated boundary, exact match) would close the
    // remainder but needs new GPU buffers — out of scope for this pass.
    if (chunk.skirt_vbo.SDLBuffer() && chunk.skirt_ibo.SDLBuffer()) {
        SDL_GPUBufferBinding skirt_vb { chunk.skirt_vbo.SDLBuffer(), 0u };
        SDL_BindGPUVertexBuffers(rp, 0, &skirt_vb, 1);
        SDL_GPUBufferBinding skirt_ib { chunk.skirt_ibo.SDLBuffer(), 0u };
        SDL_BindGPUIndexBuffer(rp, &skirt_ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_DrawGPUIndexedPrimitives(rp, (uint32_t)TERRAIN_SKIRT_IDX, 1, 0, 0, 0);
    }
#endif
}

void TerrainRenderer::Draw(GpuCommandBuffer& cb,
                           const TerrainChunk& chunk,
                           const float* vp16,
                           const SunParams& sun,
                           float cam_x, float cam_y, float cam_z,
                           float world_origin_x,
                           float world_origin_z,
                           float world_to_uv)
{
    if (!IsReady() || !chunk.loaded || !chunk.vbo.SDLBuffer() || !chunk.ibo.SDLBuffer()) return;

#ifdef MD_SDL_GPU
    cb.BindPipeline(&pipeline_);

    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(cb.SDLPass(), 0, &vb, 1);

    SDL_GPUBufferBinding ib { chunk.ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(cb.SDLPass(), &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    TerrainVertUBO vubo;
    memcpy(vubo.vp, vp16, 64);
    vubo.world_origin_x = world_origin_x;
    vubo.world_origin_z = world_origin_z;
    vubo.world_to_uv    = world_to_uv;
    vubo.lod_blend      = 0.f;
    vubo.cam_pos_ws[0] = cam_x; vubo.cam_pos_ws[1] = cam_y;
    vubo.cam_pos_ws[2] = cam_z; vubo.cam_pos_ws[3] = 0.f;
    vubo.chunk_origin_x = chunk.center_x - CHUNK_SIZE * 0.5f;
    vubo.chunk_origin_z = chunk.center_z - CHUNK_SIZE * 0.5f;
    cb.PushVertexUniforms(0, &vubo, sizeof(vubo));

    const auto& fog = GraphicsSettings::Get();
    // Linear fog, tied to terrain_cr_m — see BeginRawBatch's comment.
    const float fog_far = RenderQualityConfig::Get().terrain_cr_m;
    TerrainFragUBO fubo{};  // {} zero-inits use_zone_lookup — never set below (task #182 fix)
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0]= world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2]= world_to_uv;    fubo.world_params[3] = 0.f;
    fubo.ground_layers_a[0] = chunk.ground_layers[0]; fubo.ground_layers_a[1] = chunk.ground_layers[1];
    fubo.ground_layers_a[2] = chunk.ground_layers[2]; fubo.ground_layers_a[3] = chunk.ground_layers[3];
    fubo.ground_layers_b[0] = chunk.ground_layers[4]; fubo.ground_layers_b[1] = chunk.ground_layers[5];
    fubo.ground_layers_b[2] = fog_far; fubo.ground_layers_b[3] = 0.f;
    fubo.fog_color_near[0] = fog.fog_color[0]; fubo.fog_color_near[1] = fog.fog_color[1];
    fubo.fog_color_near[2] = fog.fog_color[2]; fubo.fog_color_near[3] = fog.fog_near_ratio * fog_far;
    fubo.blend_layers[0] = chunk.blend_layers[0]; fubo.blend_layers[1] = chunk.blend_layers[1];
    fubo.blend_layers[2] = chunk.blend_layers[2]; fubo.blend_layers[3] = chunk.blend_layers[3];
    cb.PushFragmentUniforms(0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[5];
    FillSamplerBindings(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    cb.BindFragmentSamplers(0, bindings, 5);

    SDL_GPUBuffer* sbufs[3];
    FillStorageBindings(sbufs, &chunk);
    SDL_BindGPUFragmentStorageBuffers(cb.SDLPass(), 0, sbufs, 3);

    SDL_DrawGPUIndexedPrimitives(cb.SDLPass(), TERRAIN_IDX, 1, 0, 0, 0);
#endif
}

void TerrainRenderer::BeginRawBatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                     const float* vp16, const SunParams& sun,
                                     float cam_x, float cam_y, float cam_z,
                                     float world_origin_x, float world_origin_z,
                                     float world_to_uv, int lod,
                                     float pos_offset_x, float pos_offset_z)
{
#ifdef MD_SDL_GPU
    if (!IsReady()) return;
    int lod_clamped = (lod < 1) ? 1 : (lod > TERRAIN_LOD_LEVELS) ? TERRAIN_LOD_LEVELS : lod;

    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());

    TerrainVertUBO vubo;
    memcpy(vubo.vp, vp16, 64);
    vubo.world_origin_x = world_origin_x;
    vubo.world_origin_z = world_origin_z;
    vubo.world_to_uv    = world_to_uv;
    vubo.lod_blend      = 0.f;
    vubo.cam_pos_ws[0] = cam_x; vubo.cam_pos_ws[1] = cam_y;
    vubo.cam_pos_ws[2] = cam_z; vubo.cam_pos_ws[3] = 0.f;
    // No single chunk at BeginRawBatch time (per-chunk draws happen later
    // via DrawRawChunk, the editor's zone-lookup batch path — out of scope
    // for the albedo bake this phase, see terrain_forward.slang's
    // use_lookup branch) — harmless, that branch never reads vAlbedoUV.
    vubo.chunk_origin_x = 0.f;
    vubo.chunk_origin_z = 0.f;
    vubo.pos_offset_x   = pos_offset_x;
    vubo.pos_offset_z   = pos_offset_z;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    // Cache sun/world/fog params; ground_layers varies per chunk (per-biome)
    // so it can't be baked into this shared batch push — DrawRawChunk
    // re-pushes the fragment UBO per chunk with ground_layers filled in
    // (still avoids the per-chunk pipeline/sampler binds this batching
    // exists to save).
    const auto& fog = GraphicsSettings::Get();
    // Linear fog, tied to terrain_cr_m (2026-07-12) — see graphics_settings.h's
    // fog_near_ratio comment. fog_t reaches 1.0 exactly at the draw-distance
    // cutoff, hiding the outermost LOD-crossfade dither band for free.
    const float fog_far = RenderQualityConfig::Get().terrain_cr_m;
    batch_fubo_base_.sun_dir_str[0]=sun.dir[0]; batch_fubo_base_.sun_dir_str[1]=sun.dir[1];
    batch_fubo_base_.sun_dir_str[2]=sun.dir[2]; batch_fubo_base_.sun_dir_str[3]=sun.strength;
    batch_fubo_base_.ambient[0]=sun.ambient[0]; batch_fubo_base_.ambient[1]=sun.ambient[1];
    batch_fubo_base_.ambient[2]=sun.ambient[2]; batch_fubo_base_.ambient[3]=0.f;
    batch_fubo_base_.world_params[0]=world_origin_x; batch_fubo_base_.world_params[1]=world_origin_z;
    batch_fubo_base_.world_params[2]=world_to_uv;    batch_fubo_base_.world_params[3]=0.f;
    batch_fubo_base_.ground_layers_b[2] = fog_far;
    batch_fubo_base_.fog_color_near[0] = fog.fog_color[0]; batch_fubo_base_.fog_color_near[1] = fog.fog_color[1];
    batch_fubo_base_.fog_color_near[2] = fog.fog_color[2]; batch_fubo_base_.fog_color_near[3] = fog.fog_near_ratio * fog_far;
    // Explicit reset — see TerrainFragUBO::use_zone_lookup's doc comment:
    // must not leak a stale 1.0 from an earlier synthesis/compact-LOD2 draw
    // this same frame into the normal per-chunk DrawRawChunk path below.
    batch_fubo_base_.use_zone_lookup[0] = 0.f;

    SDL_GPUTextureSamplerBinding bindings[5];
    FillSamplerBindings(bindings);
    if (bindings[0].texture && bindings[0].sampler)
        SDL_BindGPUFragmentSamplers(rp, 0, bindings, 5);

    // Always bound (cheap, read-only) — b5 (zoneGroundLayers) only actually
    // read by the shader when use_zone_lookup=1 (synthesis/compact-LOD2
    // draws); b6 (per-chunk steepness) is never read on this batch path
    // (BeginRawBatch has no single "current chunk" — same reasoning as
    // chunk_origin_x/z=0,0 above) but must still be bound to something
    // valid since the pipeline declares 2 storage buffers. DrawRawChunk
    // re-binds b6 per-chunk right after this for the LOD1 individual-chunk
    // sub-path, which DOES read it.
    SDL_GPUBuffer* sbufs[3];
    FillStorageBindings(sbufs, nullptr);
    if (sbufs[0] || sbufs[1])
        SDL_BindGPUFragmentStorageBuffers(rp, 0, sbufs, 3);

    if (lod_ibo_shared_[lod_clamped-1].SDLBuffer()) {
        SDL_GPUBufferBinding ib { lod_ibo_shared_[lod_clamped-1].SDLBuffer(), 0u };
        SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    }
    batch_idx_count_ = (uint32_t)TERRAIN_LOD_IDX[lod_clamped-1];
#endif
}

void TerrainRenderer::DrawRawChunk(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                    const TerrainChunk& chunk)
{
#ifdef MD_SDL_GPU
    if (!chunk.loaded || !chunk.vbo.SDLBuffer()) return;
    // Copies batch_fubo_base_'s sun/world/fog fields (incl. ground_layers_b[2]=fog_density
    // already set by BeginRawBatch) — only overwrite the per-chunk biome indices.
    TerrainFragUBO fubo = batch_fubo_base_;
    fubo.ground_layers_a[0] = chunk.ground_layers[0]; fubo.ground_layers_a[1] = chunk.ground_layers[1];
    fubo.ground_layers_a[2] = chunk.ground_layers[2]; fubo.ground_layers_a[3] = chunk.ground_layers[3];
    fubo.ground_layers_b[0] = chunk.ground_layers[4]; fubo.ground_layers_b[1] = chunk.ground_layers[5];
    fubo.blend_layers[0] = chunk.blend_layers[0]; fubo.blend_layers[1] = chunk.blend_layers[1];
    fubo.blend_layers[2] = chunk.blend_layers[2]; fubo.blend_layers[3] = chunk.blend_layers[3];
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    // Task #182: re-bind b6 with THIS chunk's own baked steepness (BeginRawBatch
    // bound the shared fallback, since it has no single "current chunk").
    SDL_GPUBuffer* sbufs[3];
    FillStorageBindings(sbufs, &chunk);
    if (sbufs[0] || sbufs[1])
        SDL_BindGPUFragmentStorageBuffers(rp, 0, sbufs, 3);

    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_DrawGPUIndexedPrimitives(rp, batch_idx_count_, 1, 0, 0, 0);
#endif
}

void TerrainRenderer::SetBatchGroundLayers(SDL_GPUCommandBuffer* cmd, const float ground_layers[6],
                                            float fog_density_override) {
#ifdef MD_SDL_GPU
    batch_fubo_base_.ground_layers_a[0] = ground_layers[0];
    batch_fubo_base_.ground_layers_a[1] = ground_layers[1];
    batch_fubo_base_.ground_layers_a[2] = ground_layers[2];
    batch_fubo_base_.ground_layers_a[3] = ground_layers[3];
    batch_fubo_base_.ground_layers_b[0] = ground_layers[4];
    batch_fubo_base_.ground_layers_b[1] = ground_layers[5];
    if (fog_density_override > 0.f) batch_fubo_base_.ground_layers_b[2] = fog_density_override;
    SDL_PushGPUFragmentUniformData(cmd, 0, &batch_fubo_base_, sizeof(batch_fubo_base_));
#else
    (void)cmd; (void)ground_layers; (void)fog_density_override;
#endif
}

void TerrainRenderer::SetBatchFogDensity(SDL_GPUCommandBuffer* cmd, float fog_density) {
#ifdef MD_SDL_GPU
    batch_fubo_base_.ground_layers_b[2] = fog_density;
    SDL_PushGPUFragmentUniformData(cmd, 0, &batch_fubo_base_, sizeof(batch_fubo_base_));
#else
    (void)cmd; (void)fog_density;
#endif
}

void TerrainRenderer::UploadZoneGroundLayers(const uint32_t* data, int count_uints) {
#ifdef MD_SDL_GPU
    if (count_uints != 64 * 64 * 6) {
        fprintf(stderr, "[TerrainRenderer] UploadZoneGroundLayers: expected %d uints, got %d — skipped\n",
                64 * 64 * 6, count_uints);
        return;
    }
    zone_layers_ssbo_.Upload(data, count_uints * (int)sizeof(uint32_t));
#else
    (void)data; (void)count_uints;
#endif
}

void TerrainRenderer::SetBatchZoneLookup(SDL_GPUCommandBuffer* cmd, bool enable, float fog_density_override) {
#ifdef MD_SDL_GPU
    batch_fubo_base_.use_zone_lookup[0] = enable ? 1.0f : 0.0f;
    if (fog_density_override > 0.f) batch_fubo_base_.ground_layers_b[2] = fog_density_override;
    SDL_PushGPUFragmentUniformData(cmd, 0, &batch_fubo_base_, sizeof(batch_fubo_base_));
#else
    (void)cmd; (void)enable; (void)fog_density_override;
#endif
}

