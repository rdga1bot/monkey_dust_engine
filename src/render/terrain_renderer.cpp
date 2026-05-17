#include <monkey_dust/render/terrain_renderer.h>
#include <cstdio>
#include <cstring>

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
    pd.layout.stride     = 48;
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };  // aPos
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };  // aNormal
    pd.layout.attribs[2] = { 2, 24, GpuAttribFmt::F2 };  // aUV
    pd.layout.attribs[3] = { 3, 32, GpuAttribFmt::F4 };  // aSplat

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;  // SDL_GPU Vulkan Y-flip inverts winding vs OpenGL convention
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;  // slot 0: TerrainVertUBO (80 bytes)
    pd.frag_uniform_bufs = 1;  // slot 0: SunParams      (32 bytes)
    pd.frag_samplers     = 4;  // set=2 binding=0..3: grass/rock/dirt/bark

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

    return pipeline_.Create(pd);
}

void TerrainRenderer::Shutdown() {
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

bool TerrainRenderer::IsReady() const {
#ifdef MD_SDL_GPU
    return pipeline_.SDLPipeline() != nullptr;
#else
    return false;
#endif
}

bool TerrainRenderer::InitTextures(const char* grass, const char* rock,
                                   const char* dirt,  const char* bark)
{
    const char* paths[4] = { grass, rock, dirt, bark };
    const char* names[4] = { "grass", "rock", "dirt", "bark" };

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
        if (!tex_[i].InitFromFile(paths[i], sd)) {
            fprintf(stderr, "[TerrainRenderer] texture '%s' failed to load: %s\n",
                    names[i], paths[i]);
            all_ok = false;
        }
    }
    tex_loaded_ = true;
    return all_ok;
}

#ifdef MD_SDL_GPU
void TerrainRenderer::FillSamplerBindings(SDL_GPUTextureSamplerBinding out[4]) const
{
    for (int i = 0; i < 4; ++i) {
        bool valid = tex_loaded_ && tex_[i].Valid()
                     && tex_[i].SDLTexture() && tex_[i].SDLSampler();
        out[i].texture = valid ? tex_[i].SDLTexture() : fallback_tex_;
        out[i].sampler = valid ? tex_[i].SDLSampler() : fallback_sampler_;
    }
}
#endif

void TerrainRenderer::DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                              const TerrainChunk& chunk,
                              const float* vp16,
                              const SunParams& sun,
                              float uv_scale)
{
    if (!IsReady() || !chunk.loaded) return;
#ifdef MD_SDL_GPU
    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());
    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib { chunk.ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // Vertex UBO: 80 bytes (mat4 vp + float uv_scale + float _pad[3])
    TerrainVertUBO vubo;
    memcpy(vubo.vp, vp16, 64);
    vubo.uv_scale = uv_scale;
    vubo._pad[0] = vubo._pad[1] = vubo._pad[2] = 0.f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    SDL_PushGPUFragmentUniformData(cmd, 0, &sun, 32);

    SDL_GPUTextureSamplerBinding bindings[4];
    FillSamplerBindings(bindings);
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 4);

    SDL_DrawGPUIndexedPrimitives(rp, TERRAIN_IDX, 1, 0, 0, 0);
#endif
}

void TerrainRenderer::Draw(GpuCommandBuffer& cb,
                           const TerrainChunk& chunk,
                           const float* vp16,
                           const SunParams& sun,
                           float uv_scale)
{
    if (!IsReady() || !chunk.loaded) return;

#ifdef MD_SDL_GPU
    cb.BindPipeline(&pipeline_);

    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(cb.SDLPass(), 0, &vb, 1);

    SDL_GPUBufferBinding ib { chunk.ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(cb.SDLPass(), &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // Vertex UBO: 80 bytes
    TerrainVertUBO vubo;
    memcpy(vubo.vp, vp16, 64);
    vubo.uv_scale = uv_scale;
    vubo._pad[0] = vubo._pad[1] = vubo._pad[2] = 0.f;
    cb.PushVertexUniforms(0, &vubo, sizeof(vubo));

    cb.PushFragmentUniforms(0, &sun, 32);

    SDL_GPUTextureSamplerBinding bindings[4];
    FillSamplerBindings(bindings);
    cb.BindFragmentSamplers(0, bindings, 4);

    SDL_DrawGPUIndexedPrimitives(cb.SDLPass(), TERRAIN_IDX, 1, 0, 0, 0);
#endif
}
