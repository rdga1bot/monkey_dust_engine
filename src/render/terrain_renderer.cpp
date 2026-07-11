#include <monkey_dust/render/terrain_renderer.h>
#include <monkey_dust/world/biome_def.h>
#include <monkey_dust/world/clutter_gen.h>
#include <monkey_dust/tools/graphics_settings.h>
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

    // TerrainVertex layout (stride=48):
    //   loc 0: vec3 pos    offset  0
    //   loc 1: vec3 normal offset 12
    //   loc 2: vec2 uv     offset 24  (now used — vUV passed to frag)
    //   loc 3: vec4 splat  offset 32
    pd.layout.count      = 4;
    pd.layout.stride     = 52;  // pos(12)+norm(12)+uv(8)+splat(16)+morph_y(4)
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };  // aPos
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };  // aNormal
    pd.layout.attribs[2] = { 2, 24, GpuAttribFmt::F2 };  // aUV
    pd.layout.attribs[3] = { 3, 32, GpuAttribFmt::F4 };  // aSplat
    // loc 4 (morph_y at offset 48) declared only in POM pipeline

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;  // SDL_GPU Vulkan Y-flip inverts winding vs OpenGL convention
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;  // slot 0: TerrainVertUBO (80 bytes)
    pd.frag_uniform_bufs = 1;  // slot 0: TerrainFragUBO (64 bytes)
    pd.frag_samplers     = 6;  // b0=kenshi colour overlay, b1=ground DDS array, b2=detail tint, b3=overlay mask, b4=biome blend, b5=baked per-chunk albedo (matches terrain_forward.frag)
    pd.frag_storage_bufs = 1;  // b0 (set=1 in slang binding namespace): per-zone ground-layer lookup (zone_ground_layers_), used only when TerrainFragUBO.blend_layers.w>0.5 (synthesis/compact-LOD2 background draws — see SetBatchZoneLookup)
    // NOTE: normal-map array (tex_ground_nml_array_) is a POM-only pilot for
    // now (see biome_def.h's kGroundNmlPaths comment + CLAUDE_STATE plan) —
    // forward.frag (LOD1-3) intentionally NOT touched yet.

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
#endif

    if (!pipeline_.Create(pd)) return false;

#ifdef MD_SDL_GPU
    // Shared LOD IBOs — built once, reused by BeginRawBatch for all chunks.
    // Sized from TERRAIN_LOD_IDX[0] (largest LOD level), not a hardcoded
    // literal — same overflow risk as terrain_upload.cpp's s_lod_tmp.
    static uint16_t s_tmp[TERRAIN_LOD_IDX[0]];
    for (int li = 0; li < TERRAIN_LOD_LEVELS; ++li) {
        int step = TERRAIN_LOD_STEPS[li];
        int G = TERRAIN_GRID / step, S = TERRAIN_GRID + 1, ii = 0;
        for (int row = 0; row < G; ++row)
            for (int col = 0; col < G; ++col) {
                uint16_t bl=(uint16_t)(row*step*S+col*step);
                uint16_t br=(uint16_t)(row*step*S+col*step+step);
                uint16_t tl=(uint16_t)((row+1)*step*S+col*step);
                uint16_t tr=(uint16_t)((row+1)*step*S+col*step+step);
                s_tmp[ii++]=bl; s_tmp[ii++]=br; s_tmp[ii++]=tl;
                s_tmp[ii++]=br; s_tmp[ii++]=tr; s_tmp[ii++]=tl;
            }
        lod_ibo_shared_[li].Init(0x8893u, s_tmp, sizeof(uint16_t)*TERRAIN_LOD_IDX[li]);
    }

    // Per-zone (64x64=4096) ground-layer lookup — see UploadZoneGroundLayers.
    // 6 uint32 per zone (base,slope,cliff,grass,dirt,road GroundTexLayer
    // indices), flat index = zone_idx*6 + layer. Allocated empty here;
    // populated once by the caller (World3D editor's synthesis-mesh init).
    zone_layers_ssbo_.Init(64 * 64 * 6 * (int)sizeof(uint32_t));
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
        fprintf(stderr, "[TerrainRenderer] ground texture array failed — POM will fall back to forward\n");
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
    // LINEAR now safe: v3 stores ONLY a blend weight (all 4 channels
    // identical, 0..255), no packed texture index -- matches the real
    // Kenshi blendmap.png architecture (confirmed: every channel there is
    // strictly binary 0/255, and the smooth ramp comes entirely from the
    // GPU's own bilinear sampling of that mask, not pre-blurred data). The
    // blend-target texture INDEX now comes from TerrainChunk::blend_layers
    // (a per-chunk UBO constant, mirroring Kenshi's page-constant
    // diffuseMaps1/2/3 identity) -- see terrain_gen.cpp's ARCHITECTURE NOTE.
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

bool TerrainRenderer::InitAlbedoBake()
{
#ifdef MD_SDL_GPU
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_albedo_bake.vert";
    pd.frag_path = "shaders/terrain_albedo_bake.frag";
    // Same TerrainVertex layout/stride as the runtime chunk vbo (52 bytes) —
    // only pos/normal/uv are consumed (see terrain_albedo_bake.slang's VSIn
    // comment); splat/morph_y at offsets 32/48 are simply not read.
    pd.layout.count      = 3;
    pd.layout.stride     = 52;
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };  // aPos
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };  // aNormal
    pd.layout.attribs[2] = { 2, 24, GpuAttribFmt::F2 };  // aUV

    pd.raster.depth_test  = false;
    pd.raster.depth_write = false;
    pd.raster.cull_back   = false;
    pd.has_depth_target   = false;
    pd.color_format       = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    pd.vert_uniform_bufs = 1;  // BakeVertUBO
    pd.frag_uniform_bufs = 1;  // BakeFragUBO
    pd.frag_samplers     = 3; // tex_ground, tex_overlay_mask, tex_biome_blend

    if (!albedo_bake_pipeline_.Create(pd)) {
        fprintf(stderr, "[TerrainRenderer] albedo bake pipeline failed to create\n");
        return false;
    }
    albedo_bake_ready_ = true;
    return true;
#else
    return false;
#endif
}

void TerrainRenderer::BakeAlbedo(SDL_GPUCommandBuffer* cmd, TerrainChunk& chunk,
                                  float world_origin_x, float world_origin_z, float world_to_uv)
{
#ifdef MD_SDL_GPU
    if (!albedo_bake_ready_ || !cmd || !chunk.loaded) return;
    if (!chunk.vbo.SDLBuffer() || !chunk.ibo.SDLBuffer()) return;

    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (!dev) return;

    if (!chunk.albedo_tex.Valid()) {
        GpuSamplerDesc sd;
        sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
        sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
        sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
        sd.gen_mipmap = true;
        // InitRenderTarget (not InitFromMemory): allocates COLOR_TARGET|
        // SAMPLER with no data upload — the render pass right below writes
        // mip 0 directly, so a zero-fill upload first would be wasted work.
        // InitFromMemory's per-call synchronous acquire/copy/submit cycle,
        // done once per chunk (81+), measurably regressed startup time
        // (confirmed: ~4s baseline -> ~16s) before this fix.
        if (!chunk.albedo_tex.InitRenderTarget(ALBEDO_BAKE_SIZE, ALBEDO_BAKE_SIZE, sd))
            return;
    }

    SDL_GPUColorTargetInfo col = {};
    col.texture     = chunk.albedo_tex.SDLTexture();
    col.load_op     = SDL_GPU_LOADOP_DONT_CARE;
    col.store_op    = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &col, 1, nullptr);
    if (!rp) return;

    SDL_BindGPUGraphicsPipeline(rp, albedo_bake_pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib { chunk.ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    BakeVertUBO vubo;
    vubo.chunk_origin_x = chunk.center_x - CHUNK_SIZE * 0.5f;
    vubo.chunk_origin_z = chunk.center_z - CHUNK_SIZE * 0.5f;
    vubo.chunk_size     = CHUNK_SIZE;
    vubo._pad           = 0.f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    BakeFragUBO fubo;
    fubo.world_params[0] = world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2] = world_to_uv;    fubo.world_params[3] = 0.f;
    fubo.ground_layers_a[0] = chunk.ground_layers[0]; fubo.ground_layers_a[1] = chunk.ground_layers[1];
    fubo.ground_layers_a[2] = chunk.ground_layers[2]; fubo.ground_layers_a[3] = chunk.ground_layers[3];
    fubo.ground_layers_b[0] = chunk.ground_layers[4]; fubo.ground_layers_b[1] = chunk.ground_layers[5];
    fubo.ground_layers_b[2] = 0.f; fubo.ground_layers_b[3] = 0.f;
    fubo.blend_layers[0] = chunk.blend_layers[0]; fubo.blend_layers[1] = chunk.blend_layers[1];
    fubo.blend_layers[2] = chunk.blend_layers[2]; fubo.blend_layers[3] = chunk.blend_layers[3];
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[3];
    bool ga = ground_array_ready_ && tex_ground_array_.Valid()
              && tex_ground_array_.SDLTexture() && tex_ground_array_.SDLSampler();
    bindings[0].texture = ga ? tex_ground_array_.SDLTexture() : nullptr;
    bindings[0].sampler = ga ? tex_ground_array_.SDLSampler() : nullptr;
    bool mv = overlay_mask_ready_ && tex_overlay_mask_.Valid()
              && tex_overlay_mask_.SDLTexture() && tex_overlay_mask_.SDLSampler();
    bindings[1].texture = mv ? tex_overlay_mask_.SDLTexture() : fallback_mask_tex_;
    bindings[1].sampler = mv ? tex_overlay_mask_.SDLSampler() : fallback_mask_sampler_;
    bool bv = biome_blend_ready_ && tex_biome_blend_.Valid()
              && tex_biome_blend_.SDLTexture() && tex_biome_blend_.SDLSampler();
    bindings[2].texture = bv ? tex_biome_blend_.SDLTexture() : fallback_blend_tex_;
    bindings[2].sampler = bv ? tex_biome_blend_.SDLSampler() : fallback_blend_sampler_;
    if (!bindings[0].texture || !bindings[0].sampler) { SDL_EndGPURenderPass(rp); return; }
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 3);

    SDL_DrawGPUIndexedPrimitives(rp, (uint32_t)TERRAIN_IDX, 1, 0, 0, 0);
    SDL_EndGPURenderPass(rp);

    // Regenerate mips from the just-baked mip-0 content — the ones created
    // at texture-allocation time (see the gen_mipmap comment above) are from
    // the zero-fill placeholder, not this chunk's real albedo.
    SDL_GenerateMipmapsForGPUTexture(cmd, chunk.albedo_tex.SDLTexture());

    chunk.albedo_baked = true;
#endif
}

void TerrainRenderer::Shutdown() {
    ShutdownPOM();
    tex_colour_.Shutdown();
    tex_ground_array_.Shutdown();
    tex_ground_nml_array_.Shutdown();
    tex_loaded_         = false;
    ground_array_ready_ = false;
    zone_layers_ssbo_.Shutdown();

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        if (fallback_sampler_) SDL_ReleaseGPUSampler(dev, fallback_sampler_);
        if (fallback_tex_)     SDL_ReleaseGPUTexture(dev, fallback_tex_);
    }
    fallback_tex_     = nullptr;
    fallback_sampler_ = nullptr;
#endif

    pipeline_.Destroy();
}

bool TerrainRenderer::IsPomReady() const {
#ifdef MD_SDL_GPU
    return pom_loaded_ && ground_array_ready_ && pom_pipeline_.SDLPipeline() != nullptr;
#else
    return false;
#endif
}

bool TerrainRenderer::InitPOM(const char* detail_path, const PomParams& p)
{
    pom_params_ = p;

    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_pom.vert";
    pd.frag_path = "shaders/terrain_pom.frag";
    pd.layout.count      = 5;
    pd.layout.stride     = 52;  // pos(12)+norm(12)+uv(8)+splat(16)+morph_y(4)
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };  // aPos
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };  // aNormal
    pd.layout.attribs[2] = { 2, 24, GpuAttribFmt::F2 };  // aUV
    pd.layout.attribs[3] = { 3, 32, GpuAttribFmt::F4 };  // aSplat
    pd.layout.attribs[4] = { 4, 48, GpuAttribFmt::F1 };  // aMorphY (geomorph target)
    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;
    pd.has_depth_target   = true;
    pd.vert_uniform_bufs  = 1;   // slot 0: TerrainPomVertUBO (96 bytes)
    pd.frag_uniform_bufs  = 1;   // slot 0: TerrainPomFragUBO (96 bytes)
    pd.frag_samplers      = 7;   // b0=tex_colour, b1=tex_ground(array), b2=tex_detail, b3=tex_overlay_mask, b4=tex_biome_blend, b5=tex_ground_nml(array), b6=baked per-chunk albedo

    fprintf(stderr, "[TerrainRenderer] creating POM pipeline...\n");
    if (!pom_pipeline_.Create(pd)) {
        fprintf(stderr, "[TerrainRenderer] POM pipeline failed to create\n");
        return false;
    }

#ifdef MD_SDL_GPU
    // 1×1 neutral fallback: grey tint + zero height → no visible POM displacement
    GpuSamplerDesc fsd;
    fsd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    fsd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    fsd.wrap_s     = GpuSamplerDesc::Wrap::REPEAT;
    fsd.wrap_t     = GpuSamplerDesc::Wrap::REPEAT;
    fsd.gen_mipmap = false;
    fsd.flip_v     = false;
    uint8_t neutral[4] = { 128, 128, 128, 0 };
    GpuTexture fb;
    if (fb.InitFromMemory(neutral, 1, 1, fsd)) {
        fallback_detail_tex_     = fb.TakeSDLTexture();
        fallback_detail_sampler_ = fb.TakeSDLSampler();
    }
    // Overlay-mask fallback: all-zero RGBA — no grass/dirt/road painted
    // anywhere, so base/slope/cliff blend alone if the mask fails to load
    // (safe default, not a wrong-looking one).
    uint8_t mask_neutral[4] = { 0, 0, 0, 0 };
    GpuTexture fbm;
    if (fbm.InitFromMemory(mask_neutral, 1, 1, fsd)) {
        fallback_mask_tex_     = fbm.TakeSDLTexture();
        fallback_mask_sampler_ = fbm.TakeSDLSampler();
    }
    // Biome-blend fallback: A=0 -- no cross-fade, pure current-zone biome
    // (safe default if the blend map fails to load).
    uint8_t blend_neutral[4] = { 0, 0, 0, 0 };
    GpuTexture fbb;
    if (fbb.InitFromMemory(blend_neutral, 1, 1, fsd)) {
        fallback_blend_tex_     = fbb.TakeSDLTexture();
        fallback_blend_sampler_ = fbb.TakeSDLSampler();
    }
#endif

    if (detail_path && detail_path[0]) {
        GpuSamplerDesc sd;
        sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
        sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
        sd.wrap_s     = GpuSamplerDesc::Wrap::REPEAT;
        sd.wrap_t     = GpuSamplerDesc::Wrap::REPEAT;
        sd.gen_mipmap = true;
        sd.flip_v     = false;
        tex_detail_.Shutdown();
        if (!tex_detail_.InitFromFile(detail_path, sd))
            fprintf(stderr, "[TerrainRenderer] POM detail '%s' failed — using neutral fallback\n",
                    detail_path);
    }

    pom_loaded_ = true;
    fprintf(stdout, "[TerrainRenderer] POM ready (scale=%.3f layers=%d-%d)\n",
            p.height_scale, p.layers_min, p.layers_max);
    return true;
}

void TerrainRenderer::ShutdownPOM()
{
    tex_detail_.Shutdown();
    tex_overlay_mask_.Shutdown();
    overlay_mask_ready_ = false;
    tex_biome_blend_.Shutdown();
    biome_blend_ready_ = false;
    pom_pipeline_.Destroy();
    pom_loaded_ = false;

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        if (fallback_detail_sampler_) SDL_ReleaseGPUSampler(dev, fallback_detail_sampler_);
        if (fallback_detail_tex_)     SDL_ReleaseGPUTexture(dev, fallback_detail_tex_);
        if (fallback_mask_sampler_)   SDL_ReleaseGPUSampler(dev, fallback_mask_sampler_);
        if (fallback_mask_tex_)       SDL_ReleaseGPUTexture(dev, fallback_mask_tex_);
        if (fallback_blend_sampler_)  SDL_ReleaseGPUSampler(dev, fallback_blend_sampler_);
        if (fallback_blend_tex_)      SDL_ReleaseGPUTexture(dev, fallback_blend_tex_);
    }
    fallback_detail_tex_     = nullptr;
    fallback_detail_sampler_ = nullptr;
    fallback_mask_tex_       = nullptr;
    fallback_mask_sampler_   = nullptr;
    fallback_blend_tex_      = nullptr;
    fallback_blend_sampler_  = nullptr;
#endif
}

#ifdef MD_SDL_GPU
void TerrainRenderer::FillPomSamplerBindings(SDL_GPUTextureSamplerBinding out[7]) const
{
    // binding 0: Kenshi overlay (world colour — biome identity)
    bool ov = tex_colour_.Valid() && tex_colour_.SDLTexture() && tex_colour_.SDLSampler();
    out[0].texture = ov ? tex_colour_.SDLTexture() : fallback_tex_;
    out[0].sampler = ov ? tex_colour_.SDLSampler() : fallback_sampler_;
    // binding 1: per-biome DDS ground array (detail modulator)
    bool ga = ground_array_ready_ && tex_ground_array_.Valid()
              && tex_ground_array_.SDLTexture() && tex_ground_array_.SDLSampler();
    out[1].texture = ga ? tex_ground_array_.SDLTexture() : nullptr;
    out[1].sampler = ga ? tex_ground_array_.SDLSampler() : nullptr;
    // binding 2: detail/height texture (POM ray marching height field)
    bool dv = tex_detail_.Valid() && tex_detail_.SDLTexture() && tex_detail_.SDLSampler();
    out[2].texture = dv ? tex_detail_.SDLTexture() : fallback_detail_tex_;
    out[2].sampler = dv ? tex_detail_.SDLSampler() : fallback_detail_sampler_;
    // binding 3: painted grass/dirt/road mask (R=grass,G=secondary,B=dirt,A=road)
    bool mv = overlay_mask_ready_ && tex_overlay_mask_.Valid()
              && tex_overlay_mask_.SDLTexture() && tex_overlay_mask_.SDLSampler();
    out[3].texture = mv ? tex_overlay_mask_.SDLTexture() : fallback_mask_tex_;
    out[3].sampler = mv ? tex_overlay_mask_.SDLSampler() : fallback_mask_sampler_;
    // binding 4: procedural biome-crossfade blend map (R/G/B=neighbour
    // base/slope/cliff idx, A=blend weight)
    bool bv = biome_blend_ready_ && tex_biome_blend_.Valid()
              && tex_biome_blend_.SDLTexture() && tex_biome_blend_.SDLSampler();
    out[4].texture = bv ? tex_biome_blend_.SDLTexture() : fallback_blend_tex_;
    out[4].sampler = bv ? tex_biome_blend_.SDLSampler() : fallback_blend_sampler_;
    // binding 5: per-biome DDS normal-map array (paired 1:1 with binding 1's
    // diffuse layers) — see biome_def.h's kGroundNmlPaths comment.
    bool na = ground_array_ready_ && tex_ground_nml_array_.Valid()
              && tex_ground_nml_array_.SDLTexture() && tex_ground_nml_array_.SDLSampler();
    out[5].texture = na ? tex_ground_nml_array_.SDLTexture() : nullptr;
    out[5].sampler = na ? tex_ground_nml_array_.SDLSampler() : nullptr;
    // binding 6: baked per-chunk albedo (see BakeAlbedo) — default fallback,
    // DrawRawPOM overwrites with chunk.albedo_tex per-draw when baked.
    out[6].texture = fallback_tex_;
    out[6].sampler = fallback_sampler_;
}
#endif

void TerrainRenderer::DrawRawPOM(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                  const TerrainChunk& chunk,
                                  const float* vp16,
                                  const SunParams& sun,
                                  float cam_x, float cam_y, float cam_z,
                                  float world_origin_x,
                                  float world_origin_z,
                                  float world_to_uv,
                                  int   lod,
                                  float fog_density_override,
                                  float lod_blend)
{
    if (!IsPomReady() || !chunk.loaded) {
        DrawRaw(rp, cmd, chunk, vp16, sun, world_origin_x, world_origin_z, world_to_uv, lod);
        return;
    }
#ifdef MD_SDL_GPU
    if (!chunk.vbo.SDLBuffer() || !chunk.ibo.SDLBuffer()) return;

    SDL_BindGPUGraphicsPipeline(rp, pom_pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);

    // LOD: use uniform lod across all chunks to avoid T-junctions at seams.
    // lod=0: full 64×64 (TERRAIN_IDX); lod=1..3: ibo_lod[lod-1].
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

    TerrainPomVertUBO vubo;
    memcpy(vubo.vp, vp16, 64);
    vubo.world_origin_x = world_origin_x;
    vubo.world_origin_z = world_origin_z;
    vubo.world_to_uv    = world_to_uv;
    vubo.lod_blend      = lod_blend;
    vubo.cam_pos_ws[0]  = cam_x;
    vubo.cam_pos_ws[1]  = cam_y;
    vubo.cam_pos_ws[2]  = cam_z;
    vubo.cam_pos_ws[3]  = 0.f;  // w unused; geomorph is per-vertex in shader
    vubo.chunk_origin_x = chunk.center_x - CHUNK_SIZE * 0.5f;
    vubo.chunk_origin_z = chunk.center_z - CHUNK_SIZE * 0.5f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    TerrainPomFragUBO fubo;
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0]= world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2]= world_to_uv;    fubo.world_params[3] = 0.f;
    const auto& fog = GraphicsSettings::Get();
    fubo.pom_params[0]  = pom_params_.height_scale;
    fubo.pom_params[1]  = (float)pom_params_.layers_min;
    fubo.pom_params[2]  = (float)pom_params_.layers_max;
    fubo.pom_params[3]  = (fog_density_override > 0.f) ? fog_density_override : fog.fog_density;
    fubo.ground_layers_a[0] = chunk.ground_layers[0];  // base
    fubo.ground_layers_a[1] = chunk.ground_layers[1];  // slope
    fubo.ground_layers_a[2] = chunk.ground_layers[2];  // cliff
    fubo.ground_layers_a[3] = chunk.ground_layers[3];  // grass
    fubo.ground_layers_b[0] = chunk.ground_layers[4];  // dirt
    fubo.ground_layers_b[1] = chunk.ground_layers[5];  // road
    fubo.ground_layers_b[2] = 0.f;  // unused
    fubo.ground_layers_b[3] = 0.f;
    fubo.blend_layers[0] = chunk.blend_layers[0];
    fubo.blend_layers[1] = chunk.blend_layers[1];
    fubo.blend_layers[2] = chunk.blend_layers[2];
    fubo.blend_layers[3] = chunk.blend_layers[3];  // slot B (second differing neighbour) base index
    fubo.fog_color_near[0] = fog.fog_color[0]; fubo.fog_color_near[1] = fog.fog_color[1];
    fubo.fog_color_near[2] = fog.fog_color[2]; fubo.fog_color_near[3] = fog.fog_near;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[7];
    FillPomSamplerBindings(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    if (chunk.albedo_baked && chunk.albedo_tex.Valid()) {
        bindings[6].texture = chunk.albedo_tex.SDLTexture();
        bindings[6].sampler = chunk.albedo_tex.SDLSampler();
    }
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 7);

    SDL_DrawGPUIndexedPrimitives(rp, idx_count, 1, 0, 0, 0);
#endif
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
void TerrainRenderer::FillSamplerBindings(SDL_GPUTextureSamplerBinding out[6]) const
{
    // UseColourOverride() swaps texture for a batch (VT local composite).
    if (col_override_tex_ && col_override_smp_) {
        out[0].texture = col_override_tex_;
        out[0].sampler = col_override_smp_;
    } else {
        bool valid = tex_loaded_ && tex_colour_.Valid()
                     && tex_colour_.SDLTexture() && tex_colour_.SDLSampler();
        out[0].texture = valid ? tex_colour_.SDLTexture() : fallback_tex_;
        out[0].sampler = valid ? tex_colour_.SDLSampler() : fallback_sampler_;
    }
    // b1: per-biome DDS ground array — same texture as the POM (LOD0) pass, so
    // distant terrain (LOD1-3, this pipeline) stays in the same material family.
    bool ga = ground_array_ready_ && tex_ground_array_.Valid()
              && tex_ground_array_.SDLTexture() && tex_ground_array_.SDLSampler();
    out[1].texture = ga ? tex_ground_array_.SDLTexture() : nullptr;
    out[1].sampler = ga ? tex_ground_array_.SDLSampler() : nullptr;
    // b2: detail tint (loaded by InitPOM; fallback=white 1×1 → neutral blend)
    bool det = pom_loaded_ && tex_detail_.Valid()
               && tex_detail_.SDLTexture() && tex_detail_.SDLSampler();
    out[2].texture = det ? tex_detail_.SDLTexture() : fallback_tex_;
    out[2].sampler = det ? tex_detail_.SDLSampler() : fallback_sampler_;
    // b3: painted grass/dirt/road mask — same texture as the POM pass.
    bool mv = overlay_mask_ready_ && tex_overlay_mask_.Valid()
              && tex_overlay_mask_.SDLTexture() && tex_overlay_mask_.SDLSampler();
    out[3].texture = mv ? tex_overlay_mask_.SDLTexture() : fallback_mask_tex_;
    out[3].sampler = mv ? tex_overlay_mask_.SDLSampler() : fallback_mask_sampler_;
    // b4: procedural biome-crossfade blend map — same texture as the POM pass.
    bool bv = biome_blend_ready_ && tex_biome_blend_.Valid()
              && tex_biome_blend_.SDLTexture() && tex_biome_blend_.SDLSampler();
    out[4].texture = bv ? tex_biome_blend_.SDLTexture() : fallback_blend_tex_;
    out[4].sampler = bv ? tex_biome_blend_.SDLSampler() : fallback_blend_sampler_;
    // b5: baked per-chunk albedo (see BakeAlbedo) — default fallback,
    // DrawRaw overwrites with chunk.albedo_tex per-draw when baked.
    out[5].texture = fallback_tex_;
    out[5].sampler = fallback_sampler_;
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
                              float lod_blend)
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
    TerrainFragUBO fubo;
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0]= world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2]= world_to_uv;    fubo.world_params[3] = 0.f;
    fubo.ground_layers_a[0] = chunk.ground_layers[0]; fubo.ground_layers_a[1] = chunk.ground_layers[1];
    fubo.ground_layers_a[2] = chunk.ground_layers[2]; fubo.ground_layers_a[3] = chunk.ground_layers[3];
    fubo.ground_layers_b[0] = chunk.ground_layers[4]; fubo.ground_layers_b[1] = chunk.ground_layers[5];
    fubo.ground_layers_b[2] = fog.fog_density; fubo.ground_layers_b[3] = 0.f;
    fubo.fog_color_near[0] = fog.fog_color[0]; fubo.fog_color_near[1] = fog.fog_color[1];
    fubo.fog_color_near[2] = fog.fog_color[2]; fubo.fog_color_near[3] = fog.fog_near;
    fubo.blend_layers[0] = chunk.blend_layers[0]; fubo.blend_layers[1] = chunk.blend_layers[1];
    fubo.blend_layers[2] = chunk.blend_layers[2]; fubo.blend_layers[3] = chunk.blend_layers[3];
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[6];
    FillSamplerBindings(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    if (chunk.albedo_baked && chunk.albedo_tex.Valid()) {
        bindings[5].texture = chunk.albedo_tex.SDLTexture();
        bindings[5].sampler = chunk.albedo_tex.SDLSampler();
    }
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 6);

    SDL_DrawGPUIndexedPrimitives(rp, idx_count, 1, 0, 0, 0);

    // Skirt only for LOD=0 (ground-level view). Distant chunks never need it.
    if (lod_clamped == 0 && chunk.skirt_vbo.SDLBuffer() && chunk.skirt_ibo.SDLBuffer()) {
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
    TerrainFragUBO fubo;
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0]= world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2]= world_to_uv;    fubo.world_params[3] = 0.f;
    fubo.ground_layers_a[0] = chunk.ground_layers[0]; fubo.ground_layers_a[1] = chunk.ground_layers[1];
    fubo.ground_layers_a[2] = chunk.ground_layers[2]; fubo.ground_layers_a[3] = chunk.ground_layers[3];
    fubo.ground_layers_b[0] = chunk.ground_layers[4]; fubo.ground_layers_b[1] = chunk.ground_layers[5];
    fubo.ground_layers_b[2] = fog.fog_density; fubo.ground_layers_b[3] = 0.f;
    fubo.fog_color_near[0] = fog.fog_color[0]; fubo.fog_color_near[1] = fog.fog_color[1];
    fubo.fog_color_near[2] = fog.fog_color[2]; fubo.fog_color_near[3] = fog.fog_near;
    fubo.blend_layers[0] = chunk.blend_layers[0]; fubo.blend_layers[1] = chunk.blend_layers[1];
    fubo.blend_layers[2] = chunk.blend_layers[2]; fubo.blend_layers[3] = chunk.blend_layers[3];
    cb.PushFragmentUniforms(0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[6];
    FillSamplerBindings(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    if (chunk.albedo_baked && chunk.albedo_tex.Valid()) {
        bindings[5].texture = chunk.albedo_tex.SDLTexture();
        bindings[5].sampler = chunk.albedo_tex.SDLSampler();
    }
    cb.BindFragmentSamplers(0, bindings, 6);

    SDL_DrawGPUIndexedPrimitives(cb.SDLPass(), TERRAIN_IDX, 1, 0, 0, 0);
#endif
}

void TerrainRenderer::BeginRawBatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                     const float* vp16, const SunParams& sun,
                                     float cam_x, float cam_y, float cam_z,
                                     float world_origin_x, float world_origin_z,
                                     float world_to_uv, int lod)
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
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    // Cache sun/world/fog params; ground_layers varies per chunk (per-biome)
    // so it can't be baked into this shared batch push — DrawRawChunk
    // re-pushes the fragment UBO per chunk with ground_layers filled in
    // (still avoids the per-chunk pipeline/sampler binds this batching
    // exists to save).
    const auto& fog = GraphicsSettings::Get();
    batch_fubo_base_.sun_dir_str[0]=sun.dir[0]; batch_fubo_base_.sun_dir_str[1]=sun.dir[1];
    batch_fubo_base_.sun_dir_str[2]=sun.dir[2]; batch_fubo_base_.sun_dir_str[3]=sun.strength;
    batch_fubo_base_.ambient[0]=sun.ambient[0]; batch_fubo_base_.ambient[1]=sun.ambient[1];
    batch_fubo_base_.ambient[2]=sun.ambient[2]; batch_fubo_base_.ambient[3]=0.f;
    batch_fubo_base_.world_params[0]=world_origin_x; batch_fubo_base_.world_params[1]=world_origin_z;
    batch_fubo_base_.world_params[2]=world_to_uv;    batch_fubo_base_.world_params[3]=0.f;
    batch_fubo_base_.ground_layers_b[2] = fog.fog_density;
    batch_fubo_base_.fog_color_near[0] = fog.fog_color[0]; batch_fubo_base_.fog_color_near[1] = fog.fog_color[1];
    batch_fubo_base_.fog_color_near[2] = fog.fog_color[2]; batch_fubo_base_.fog_color_near[3] = fog.fog_near;
    // Explicit reset — see TerrainFragUBO::use_zone_lookup's doc comment:
    // must not leak a stale 1.0 from an earlier synthesis/compact-LOD2 draw
    // this same frame into the normal per-chunk DrawRawChunk path below.
    batch_fubo_base_.use_zone_lookup[0] = 0.f;

    SDL_GPUTextureSamplerBinding bindings[6];
    FillSamplerBindings(bindings);
    if (bindings[0].texture && bindings[0].sampler)
        SDL_BindGPUFragmentSamplers(rp, 0, bindings, 6);

    // Always bound (cheap, read-only) — only actually read by the shader
    // when use_zone_lookup=1 (synthesis/compact-LOD2 draws). Harmless no-op
    // for the normal per-chunk LOD1 path above.
    if (SDL_GPUBuffer* zbuf = zone_layers_ssbo_.SDLBuffer())
        SDL_BindGPUFragmentStorageBuffers(rp, 0, &zbuf, 1);

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

