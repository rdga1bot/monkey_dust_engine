#include <monkey_dust/render/clutter_renderer.h>
#include <monkey_dust/render/prop_tex_shared.h>
#include <cstdio>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

bool ClutterRenderer::Init() {
    PropTexShared::Get().Init();  // idempotent; shared across all PropRenderer/ClutterRenderer instances
#ifdef MD_SDL_GPU
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/clutter.vert";
    pd.frag_path = "shaders/prop.frag";  // same Lambert + PropFrag UBO — no change needed

    // PropVertex layout (chunk.clutter_vbo already uses this): pos(loc=0,off=0,F3)
    // + normal(loc=1,off=12,F3) + uv(loc=2,off=24,F2) + layer(loc=3,off=32,F1), stride=36.
    pd.layout.count      = 4;
    pd.layout.stride     = 36;
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };
    pd.layout.attribs[2] = { 2, 24, GpuAttribFmt::F2 };
    pd.layout.attribs[3] = { 3, 32, GpuAttribFmt::F1 };

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;  // SDL_GPU Vulkan Y-flip inverts winding vs OpenGL
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;  // slot 0: ClutterVert UBO (64 bytes: mat4 vp only)
    pd.frag_uniform_bufs = 1;  // slot 0: PropFrag UBO (32 bytes: sun_dir_str + ambient)
    pd.frag_samplers     = 3;  // 0=tex_rock, 1=tex_veg (PropTexShared), 2=tex_dummy (unused here)

    if (!pipeline_.Create(pd)) {
        fprintf(stderr, "[ClutterRenderer] Pipeline creation failed\n");
        return false;
    }
    static const uint8_t white1x1[4] = {255, 255, 255, 255};
    tex_dummy_.InitFromMemory(white1x1, 1, 1, GpuSamplerDesc{});
    ready_ = true;
#endif
    return ready_;
}

void ClutterRenderer::Shutdown() {
    pipeline_.Destroy();
    tex_dummy_.Shutdown();
    ready_ = false;
}

void ClutterRenderer::DrawChunk(
#ifdef MD_SDL_GPU
    SDL_GPURenderPass*    rp,
    SDL_GPUCommandBuffer* cmd,
#endif
    const TerrainChunk& chunk,
    const float* vp16,
    const float* sun32)
{
    if (!ready_ || !chunk.clutter_loaded || chunk.clutter_index_count <= 0) return;

#ifdef MD_SDL_GPU
    if (!pipeline_.SDLPipeline()) return;
    if (!chunk.clutter_vbo.SDLBuffer() || !chunk.clutter_ibo.SDLBuffer()) return;

    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb { chunk.clutter_vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);

    SDL_GPUBufferBinding ib { chunk.clutter_ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    PropTexShared& pt = PropTexShared::Get();
    if (pt.ready) {
        SDL_GPUTextureSamplerBinding sb[3] = {
            { pt.tex_rock->SDLTexture(), pt.tex_rock->SDLSampler() },
            { pt.tex_veg->SDLTexture(),  pt.tex_veg->SDLSampler()  },
            { tex_dummy_.SDLTexture(),   tex_dummy_.SDLSampler()   },
        };
        SDL_BindGPUFragmentSamplers(rp, 0, sb, 3);
    }

    SDL_PushGPUVertexUniformData(cmd, 0, vp16, 64);
    SDL_PushGPUFragmentUniformData(cmd, 0, sun32, 32);

    // ONE draw call for the whole chunk's merged clutter — GPU cost is the
    // same whether this represents 5 rocks or 5000 (KEN-CLUTTER Tier 2).
    SDL_DrawGPUIndexedPrimitives(rp, (uint32_t)chunk.clutter_index_count, 1, 0, 0, 0);
#else
    (void)chunk; (void)vp16; (void)sun32;
#endif
}
