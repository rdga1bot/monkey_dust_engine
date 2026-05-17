// PropRenderer — per-instance rock prop draw (up to MAX_PROPS per frame, no malloc).
#include <monkey_dust/render/prop_renderer.h>
#include <cstring>
#include <cstdio>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

bool PropRenderer::Init(const char* glb_path) {
    if (!glb_path) {
        fprintf(stdout, "[PropRenderer] No GLB path supplied — prop draw disabled\n");
        return false;
    }

    if (!mesh_.LoadGLB(glb_path)) {
        fprintf(stdout, "[PropRenderer] GLB load failed — prop draw disabled\n");
        return false;
    }

#ifdef MD_SDL_GPU
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/prop.vert";
    pd.frag_path = "shaders/prop.frag";

    // PropVertex: pos(loc=0, offset=0, F3) + normal(loc=1, offset=12, F3), stride=24
    pd.layout.count      = 2;
    pd.layout.stride     = 24;
    pd.layout.attribs[0] = { 0,  0, GpuAttribFmt::F3 };  // aPos
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };  // aNormal

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;  // SDL_GPU Vulkan Y-flip inverts winding vs OpenGL
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;  // slot 0: PropVert UBO (80 bytes: mat4 vp + vec4 model_pos_scale)
    pd.frag_uniform_bufs = 1;  // slot 0: PropFrag UBO (32 bytes: sun_dir_str + ambient)

    if (!pipeline_.Create(pd)) {
        fprintf(stderr, "[PropRenderer] Pipeline creation failed\n");
        mesh_.Shutdown();
        return false;
    }
#endif

    return true;
}

void PropRenderer::Shutdown() {
    pipeline_.Destroy();
    mesh_.Shutdown();
}

// Vertex UBO layout (std140, 80 bytes):
//   mat4 vp            — 64 bytes (columns 0-3)
//   vec4 model_pos_scale — 16 bytes (xyz=world pos, w=scale)
struct alignas(16) PropVertUBO {
    float vp[16];              // 64 bytes
    float model_pos_scale[4];  // 16 bytes
};
static_assert(sizeof(PropVertUBO) == 80, "PropVertUBO size mismatch");

void PropRenderer::DrawRaw(
#ifdef MD_SDL_GPU
    SDL_GPURenderPass*    rp,
    SDL_GPUCommandBuffer* cmd,
#endif
    const float* positions_xyz,
    int          count,
    const float* vp16,
    const float* sun32)
{
    if (!mesh_.loaded || count <= 0) return;
    if (count > MAX_PROPS) count = MAX_PROPS;

#ifdef MD_SDL_GPU
    if (!pipeline_.SDLPipeline()) return;

    // Bind pipeline + mesh buffers once.
    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb { mesh_.vbo.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);

    SDL_GPUIndexElementSize idx_size = mesh_.indices_u16
        ? SDL_GPU_INDEXELEMENTSIZE_16BIT
        : SDL_GPU_INDEXELEMENTSIZE_32BIT;
    SDL_GPUBufferBinding ib { mesh_.ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, idx_size);

    // Push fragment UBO once (sun params are shared for all props).
    SDL_PushGPUFragmentUniformData(cmd, 0, sun32, 32);

    // Per-instance draws (no malloc, fixed UBO stack).
    PropVertUBO v_ubo;
    memcpy(v_ubo.vp, vp16, 64);

    for (int i = 0; i < count; ++i) {
        const float* p = positions_xyz + i * 3;
        v_ubo.model_pos_scale[0] = p[0];
        v_ubo.model_pos_scale[1] = p[1];
        v_ubo.model_pos_scale[2] = p[2];
        v_ubo.model_pos_scale[3] = SCALE;

        SDL_PushGPUVertexUniformData(cmd, 0, &v_ubo, sizeof(v_ubo));
        SDL_DrawGPUIndexedPrimitives(rp, mesh_.index_count, 1, 0, 0, 0);
    }
#else
    (void)positions_xyz; (void)count; (void)vp16; (void)sun32;
#endif
}
