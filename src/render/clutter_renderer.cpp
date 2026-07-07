#include <monkey_dust/render/clutter_renderer.h>
#include <cstdio>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

bool ClutterRenderer::Init() {
#ifdef MD_SDL_GPU
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/clutter.vert";
    pd.frag_path = "shaders/prop.frag";  // same Lambert + PropFrag UBO — no change needed

    // PropVertex layout (chunk.clutter_vbo already uses this): pos(loc=0,off=0,F3)
    // + normal(loc=1,off=12,F3), stride=24.
    pd.layout.count      = 2;
    pd.layout.stride     = 24;
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;  // SDL_GPU Vulkan Y-flip inverts winding vs OpenGL
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;  // slot 0: ClutterVert UBO (64 bytes: mat4 vp only)
    pd.frag_uniform_bufs = 1;  // slot 0: PropFrag UBO (32 bytes: sun_dir_str + ambient)

    if (!pipeline_.Create(pd)) {
        fprintf(stderr, "[ClutterRenderer] Pipeline creation failed\n");
        return false;
    }
    ready_ = true;
#endif
    return ready_;
}

void ClutterRenderer::Shutdown() {
    pipeline_.Destroy();
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

    SDL_PushGPUVertexUniformData(cmd, 0, vp16, 64);
    SDL_PushGPUFragmentUniformData(cmd, 0, sun32, 32);

    // ONE draw call for the whole chunk's merged clutter — GPU cost is the
    // same whether this represents 5 rocks or 5000 (KEN-CLUTTER Tier 2).
    SDL_DrawGPUIndexedPrimitives(rp, (uint32_t)chunk.clutter_index_count, 1, 0, 0, 0);
#else
    (void)chunk; (void)vp16; (void)sun32;
#endif
}
