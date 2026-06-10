#include <monkey_dust/render/terrain_renderer.h>
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
    pd.frag_uniform_bufs = 1;  // slot 0: TerrainFragUBO (48 bytes)
    pd.frag_samplers     = 2;  // binding 0: kenshi colour overlay, binding 1: detail

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
    // Neutral detail fallback (128,128,128) → detail_rgb*2=1.0 → no tint if detail not loaded.
    uint8_t neutral[4] = { 128, 128, 128, 0 };
    GpuTexture fbd;
    if (!fallback_detail_tex_ && fbd.InitFromMemory(neutral, 1, 1, sd)) {
        fallback_detail_tex_     = fbd.TakeSDLTexture();
        fallback_detail_sampler_ = fbd.TakeSDLSampler();
    }
#endif

    if (!pipeline_.Create(pd)) return false;

#ifdef MD_SDL_GPU
    // Shared LOD IBOs — built once, reused by BeginRawBatch for all chunks.
    static uint16_t s_tmp[6144];
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
#endif
    return true;
}

void TerrainRenderer::Shutdown() {
    ShutdownPOM();
    tex_colour_.Shutdown();
    for (int i = 0; i < 4; ++i) tex_[i].Shutdown();
    tex_loaded_ = false;

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
    return pom_loaded_ && pom_pipeline_.SDLPipeline() != nullptr;
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
    pd.frag_uniform_bufs  = 1;   // slot 0: TerrainPomFragUBO (64 bytes)
    pd.frag_samplers      = 2;   // binding 0: tex_colour, binding 1: tex_detail

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
    pom_pipeline_.Destroy();
    pom_loaded_ = false;

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        if (fallback_detail_sampler_) SDL_ReleaseGPUSampler(dev, fallback_detail_sampler_);
        if (fallback_detail_tex_)     SDL_ReleaseGPUTexture(dev, fallback_detail_tex_);
    }
    fallback_detail_tex_     = nullptr;
    fallback_detail_sampler_ = nullptr;
#endif
}

#ifdef MD_SDL_GPU
void TerrainRenderer::FillPomSamplerBindings(SDL_GPUTextureSamplerBinding out[2]) const
{
    // binding 0: world colour overlay (same as base renderer)
    FillSamplerBindings(out);
    // binding 1: detail/height texture
    bool dv = tex_detail_.Valid() && tex_detail_.SDLTexture() && tex_detail_.SDLSampler();
    out[1].texture = dv ? tex_detail_.SDLTexture() : fallback_detail_tex_;
    out[1].sampler = dv ? tex_detail_.SDLSampler() : fallback_detail_sampler_;
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
                                  int   lod)
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
    vubo._pad0          = 0.f;
    vubo.cam_pos_ws[0]  = cam_x;
    vubo.cam_pos_ws[1]  = cam_y;
    vubo.cam_pos_ws[2]  = cam_z;
    vubo.cam_pos_ws[3]  = 0.f;  // w unused; geomorph is per-vertex in shader
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    TerrainPomFragUBO fubo;
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0]= world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2]= world_to_uv;    fubo.world_params[3] = 0.f;
    fubo.pom_params[0]  = pom_params_.height_scale;
    fubo.pom_params[1]  = (float)pom_params_.layers_min;
    fubo.pom_params[2]  = (float)pom_params_.layers_max;
    fubo.pom_params[3]  = 0.f;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[2];
    FillPomSamplerBindings(bindings);
    if (!bindings[0].texture || !bindings[0].sampler ||
        !bindings[1].texture || !bindings[1].sampler) return;
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 2);

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

bool TerrainRenderer::InitTextures(const char* grass, const char* rock,
                                   const char* dirt,  const char* bark)
{
    // Legacy splat path — kept for non-Kenshi use. When Kenshi overlay is loaded,
    // InitKenshiOverlay takes precedence.
    const char* paths[4] = { grass, rock, dirt, bark };
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::REPEAT;
    sd.wrap_t     = GpuSamplerDesc::Wrap::REPEAT;
    sd.gen_mipmap = true;
    sd.flip_v     = false;
    bool all_ok = true;
    for (int i = 0; i < 4; ++i) {
        tex_[i].Shutdown();
        if (!tex_[i].InitFromFile(paths[i], sd)) all_ok = false;
    }
    // If no kenshi overlay loaded yet, use tex_[0] as fallback colour slot
    if (!tex_colour_.Valid() && tex_[0].Valid()) {
        tex_colour_ = tex_[0];
    }
    tex_loaded_ = true;
    return all_ok;
}

#ifdef MD_SDL_GPU
void TerrainRenderer::FillSamplerBindings(SDL_GPUTextureSamplerBinding out[1]) const
{
    // UseColourOverride() swaps texture for a batch (VT local composite).
    if (col_override_tex_ && col_override_smp_) {
        out[0].texture = col_override_tex_;
        out[0].sampler = col_override_smp_;
        return;
    }
    bool valid = tex_loaded_ && tex_colour_.Valid()
                 && tex_colour_.SDLTexture() && tex_colour_.SDLSampler();
    out[0].texture = valid ? tex_colour_.SDLTexture() : fallback_tex_;
    out[0].sampler = valid ? tex_colour_.SDLSampler() : fallback_sampler_;
}
#endif

void TerrainRenderer::DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                              const TerrainChunk& chunk,
                              const float* vp16,
                              const SunParams& sun,
                              float world_origin_x,
                              float world_origin_z,
                              float world_to_uv,
                              int   lod)
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
    vubo._pad           = 0.f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    TerrainFragUBO fubo;
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0]= world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2]= world_to_uv;    fubo.world_params[3] = 0.f;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[2];
    FillPomSamplerBindings(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 2);

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
    vubo._pad           = 0.f;
    cb.PushVertexUniforms(0, &vubo, sizeof(vubo));

    TerrainFragUBO fubo;
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0]= world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2]= world_to_uv;    fubo.world_params[3] = 0.f;
    cb.PushFragmentUniforms(0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[2];
    FillPomSamplerBindings(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    cb.BindFragmentSamplers(0, bindings, 2);

    SDL_DrawGPUIndexedPrimitives(cb.SDLPass(), TERRAIN_IDX, 1, 0, 0, 0);
#endif
}

void TerrainRenderer::BeginRawBatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                     const float* vp16, const SunParams& sun,
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
    vubo._pad           = 0.f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    TerrainFragUBO fubo;
    fubo.sun_dir_str[0]=sun.dir[0]; fubo.sun_dir_str[1]=sun.dir[1];
    fubo.sun_dir_str[2]=sun.dir[2]; fubo.sun_dir_str[3]=sun.strength;
    fubo.ambient[0]=sun.ambient[0]; fubo.ambient[1]=sun.ambient[1];
    fubo.ambient[2]=sun.ambient[2]; fubo.ambient[3]=0.f;
    fubo.world_params[0]=world_origin_x; fubo.world_params[1]=world_origin_z;
    fubo.world_params[2]=world_to_uv;    fubo.world_params[3]=0.f;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[2];
    FillPomSamplerBindings(bindings);
    if (bindings[0].texture && bindings[0].sampler)
        SDL_BindGPUFragmentSamplers(rp, 0, bindings, 2);

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
    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_DrawGPUIndexedPrimitives(rp, batch_idx_count_, 1, 0, 0, 0);
#endif
}

// ── GPU Synthesis ─────────────────────────────────────────────────────────────

bool TerrainRenderer::IsSynthReady() const {
#ifdef MD_SDL_GPU
    return synth_grid_n_ > 0 && synth_pipeline_.SDLPipeline() && synth_ibo_.SDLBuffer();
#else
    return false;
#endif
}

bool TerrainRenderer::InitSynth(const uint8_t* heights, int tex_w, int tex_h,
                                 float world_ox, float world_oz,
                                 float world_size, float height_max, int grid_n)
{
    ShutdownSynth();
    synth_grid_n_   = grid_n;
    synth_world_ox_ = world_ox;
    synth_world_oz_ = world_oz;
    synth_world_size_= world_size;
    synth_height_max_= height_max;

    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_synth.vert";
    pd.frag_path = "shaders/terrain_synth.frag";
    pd.layout.count  = 0;    // no vertex attributes — positions computed in shader
    pd.layout.stride = 0;
    pd.vert_samplers     = 1;  // set=0 b=0: heightmap
    pd.vert_uniform_bufs = 1;  // set=1 b=0: SynthVertUBO
    pd.frag_samplers     = 1;  // set=2 b=0: kenshi colour overlay
    pd.frag_uniform_bufs = 1;  // set=3 b=0: SynthFragUBO
    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;
    pd.has_depth_target   = true;
    if (!synth_pipeline_.Create(pd)) {
        fprintf(stderr, "[Terrain] synth pipeline failed\n");
        return false;
    }

    // Build uint32 IBO for grid_n × grid_n quads
    const int N1 = grid_n + 1;
    const int idx_count = grid_n * grid_n * 6;
    uint32_t* idx = new uint32_t[idx_count];
    int ii = 0;
    for (int row = 0; row < grid_n; ++row)
        for (int col = 0; col < grid_n; ++col) {
            uint32_t bl = (uint32_t)(row * N1 + col);
            uint32_t br = bl + 1;
            uint32_t tl = bl + (uint32_t)N1;
            uint32_t tr = tl + 1;
            idx[ii++]=bl; idx[ii++]=br; idx[ii++]=tl;
            idx[ii++]=br; idx[ii++]=tr; idx[ii++]=tl;
        }
    synth_ibo_.Init(0x8893u, idx, sizeof(uint32_t) * idx_count);
    delete[] idx;

    // Upload heightmap texture (RGBA8, height in R channel)
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR_MIPMAP;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = true;
    sd.flip_v     = false;
    if (!synth_hmap_.InitFromMemory(heights, tex_w, tex_h, sd)) {
        fprintf(stderr, "[Terrain] synth heightmap texture failed\n");
        return false;
    }
    fprintf(stdout, "[Terrain] GPU Synthesis ready: %dx%d grid, %dx%d hmap\n",
            grid_n, grid_n, tex_w, tex_h);
    return true;
}

void TerrainRenderer::ShutdownSynth() {
    synth_pipeline_.Destroy();
    synth_ibo_.Shutdown();
    synth_hmap_.Shutdown();
    synth_grid_n_ = 0;
}

void TerrainRenderer::DrawSynth(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                 const float* vp16, const SunParams& sun,
                                 float cam_x, float cam_y, float cam_z,
                                 float world_origin_x, float world_origin_z,
                                 float world_to_uv)
{
#ifdef MD_SDL_GPU
    if (!IsSynthReady()) return;

    SDL_BindGPUGraphicsPipeline(rp, synth_pipeline_.SDLPipeline());

    // No VBO — bind dummy empty buffer to avoid driver warnings
    SDL_GPUBufferBinding dummy_vb { nullptr, 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &dummy_vb, 0);

    SDL_GPUBufferBinding ib { synth_ibo_.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    // Vertex sampler: heightmap
    SDL_GPUTextureSamplerBinding hmap_binding {
        synth_hmap_.SDLTexture(), synth_hmap_.SDLSampler()
    };
    if (!hmap_binding.texture || !hmap_binding.sampler) return;
    SDL_BindGPUVertexSamplers(rp, 0, &hmap_binding, 1);

    // Vertex UBO
    SynthVertUBO vubo;
    memcpy(vubo.vp, vp16, 64);
    vubo.world_ox    = synth_world_ox_;
    vubo.world_oz    = synth_world_oz_;
    vubo.world_size  = synth_world_size_;
    vubo.height_max  = synth_height_max_;
    vubo.cam_pos[0]  = cam_x; vubo.cam_pos[1] = cam_y;
    vubo.cam_pos[2]  = cam_z; vubo.cam_pos[3] = 0.f;
    vubo.grid_n      = (float)synth_grid_n_;
    vubo._pad[0] = vubo._pad[1] = vubo._pad[2] = 0.f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    // Fragment sampler: kenshi colour overlay
    SDL_GPUTextureSamplerBinding colour_binding;
    FillSamplerBindings(&colour_binding);
    if (!colour_binding.texture || !colour_binding.sampler) return;
    SDL_BindGPUFragmentSamplers(rp, 0, &colour_binding, 1);

    // Fragment UBO
    SynthFragUBO fubo;
    fubo.sun_dir_str[0]=sun.dir[0]; fubo.sun_dir_str[1]=sun.dir[1];
    fubo.sun_dir_str[2]=sun.dir[2]; fubo.sun_dir_str[3]=sun.strength;
    fubo.ambient[0]=sun.ambient[0]; fubo.ambient[1]=sun.ambient[1];
    fubo.ambient[2]=sun.ambient[2]; fubo.ambient[3]=0.f;
    fubo.world_params[0]=world_origin_x; fubo.world_params[1]=world_origin_z;
    fubo.world_params[2]=world_to_uv;    fubo.world_params[3]=0.f;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    uint32_t idx_count = (uint32_t)(synth_grid_n_ * synth_grid_n_ * 6);
    SDL_DrawGPUIndexedPrimitives(rp, idx_count, 1, 0, 0, 0);
#endif
}
