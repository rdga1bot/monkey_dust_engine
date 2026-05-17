#include <monkey_dust/render/terrain_renderer.h>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

bool TerrainRenderer::Init() {
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_forward.vert";
    pd.frag_path = "shaders/terrain_forward.frag";

    // TerrainVertex layout (stride=48):
    //   loc 0: vec3 pos    offset  0
    //   loc 1: vec3 normal offset 12
    //   loc 2: vec2 uv     offset 24 (unused in forward pass, must match stride)
    //   loc 3: vec4 splat  offset 32
    // Note: location 2 (aUV) is optimised away by glslc (unused in forward shader).
    // Declare only the locations that exist in the SPIR-V to avoid SDL_GPU validation errors.
    pd.layout.count      = 3;
    pd.layout.stride     = 48;  // stride still 48 — UV bytes (24-31) are silently skipped
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };  // aPos
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };  // aNormal
    pd.layout.attribs[2] = { 3, 32, GpuAttribFmt::F4 };  // aSplat

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;  // SDL_GPU Vulkan Y-flip inverts winding vs OpenGL convention
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;  // slot 0: VP matrix (64 bytes)
    pd.frag_uniform_bufs = 1;  // slot 0: SunParams  (32 bytes)

    return pipeline_.Create(pd);
}

void TerrainRenderer::Shutdown() {
    pipeline_.Destroy();
}

bool TerrainRenderer::IsReady() const {
#ifdef MD_SDL_GPU
    return pipeline_.SDLPipeline() != nullptr;
#else
    return false;
#endif
}

void TerrainRenderer::DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                              const TerrainChunk& chunk,
                              const float* vp16,
                              const SunParams& sun)
{
    if (!IsReady() || !chunk.loaded) return;
#ifdef MD_SDL_GPU
    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());
    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib { chunk.ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_PushGPUVertexUniformData(cmd, 0, vp16, 64);
    SDL_PushGPUFragmentUniformData(cmd, 0, &sun, 32);
    SDL_DrawGPUIndexedPrimitives(rp, TERRAIN_IDX, 1, 0, 0, 0);
#endif
}

void TerrainRenderer::Draw(GpuCommandBuffer& cb,
                           const TerrainChunk& chunk,
                           const float* vp16,
                           const SunParams& sun)
{
    if (!IsReady() || !chunk.loaded) return;

#ifdef MD_SDL_GPU
    cb.BindPipeline(&pipeline_);

    SDL_GPUBufferBinding vb { chunk.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(cb.SDLPass(), 0, &vb, 1);

    SDL_GPUBufferBinding ib { chunk.ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(cb.SDLPass(), &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    cb.PushVertexUniforms(0, vp16, 64);
    cb.PushFragmentUniforms(0, &sun, 32);

    SDL_DrawGPUIndexedPrimitives(cb.SDLPass(), TERRAIN_IDX, 1, 0, 0, 0);
#endif
}
