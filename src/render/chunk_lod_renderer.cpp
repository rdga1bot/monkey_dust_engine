#include <monkey_dust/render/chunk_lod_renderer.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/platform/md_log.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
// Field order matches shaders/chunk_lod.vert's ChunkLodUBO exactly (std140).
struct ChunkLodUBO {
    float vp[16];
    float origin_xyz_pad[4];
};
static_assert(sizeof(ChunkLodUBO) == 64 + 16, "ChunkLodUBO size mismatch");

struct EngineVertex { float x, y, z, nx, ny, nz; }; // matches tools/chunklod_bake's export layout exactly
} // namespace

bool ChunkLodRenderer::Init(SDL_GPUDevice* /*dev*/) {
    GpuPipeline::Desc pd;
    pd.layout.count = 2;
    pd.layout.attribs[0] = { 0, 0,  GpuAttribFmt::F3 };
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };
    pd.layout.stride = sizeof(EngineVertex);
    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = true;
    pd.has_depth_target   = true;
    pd.vert_uniform_bufs  = 1;
    pd.vert_samplers      = 0;
    pd.vert_path = "shaders/chunk_lod.vert";
    pd.frag_path = "shaders/terrain_gbuffer_mini.frag"; // reused unmodified -- see chunk_lod.vert's doc comment
    pd.frag_uniform_bufs = 0;
    pd.frag_samplers     = 0;
    pd.frag_storage_bufs = 0;
    pd.color_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT; // matches TerrainShadingProjected's gbuf_color_
    if (!gbuffer_pipeline_.Create(pd)) {
        MD_LOG(MD_LOG_WARNING, "[ChunkLodRenderer] pipeline create failed");
        return false;
    }
    ready_ = true;
    return true;
}

void ChunkLodRenderer::Shutdown(SDL_GPUDevice* /*dev*/) {
    if (mesh_loaded_) { vbo_.Shutdown(); ibo_.Shutdown(); mesh_loaded_ = false; }
    gbuffer_pipeline_.Destroy();
    ready_ = false;
}

bool ChunkLodRenderer::LoadMesh(SDL_GPUDevice* /*dev*/, const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "[ChunkLodRenderer] cannot open %s", path);
        return false;
    }
    uint32_t vcount = 0, icount = 0;
    bool ok = std::fread(&vcount, sizeof(vcount), 1, f) == 1 &&
              std::fread(&icount, sizeof(icount), 1, f) == 1;
    std::vector<EngineVertex> verts;
    std::vector<uint32_t> indices;
    if (ok) {
        verts.resize(vcount);
        indices.resize(icount);
        ok = std::fread(verts.data(), sizeof(EngineVertex), vcount, f) == vcount &&
             std::fread(indices.data(), sizeof(uint32_t), icount, f) == icount;
    }
    std::fclose(f);
    if (!ok || vcount == 0 || icount == 0) {
        MD_LOG(MD_LOG_WARNING, "[ChunkLodRenderer] bad mesh file %s", path);
        return false;
    }

    if (mesh_loaded_) { vbo_.Shutdown(); ibo_.Shutdown(); }
    vbo_.Init(0x8892u /*GL_ARRAY_BUFFER*/, verts.data(), (uint32_t)(verts.size() * sizeof(EngineVertex)));
    ibo_.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, indices.data(), (uint32_t)(indices.size() * sizeof(uint32_t)));
    index_count_ = icount;
    mesh_loaded_ = true;
    MD_LOG(MD_LOG_INFO, "[ChunkLodRenderer] loaded %s: %u verts, %u indices (%u triangles)",
           path, vcount, icount, icount / 3);
    return true;
}

void ChunkLodRenderer::Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                            const float* vp16, const float* origin_xyz) {
    if (!ready_ || !mesh_loaded_) return;

    SDL_BindGPUGraphicsPipeline(rp, gbuffer_pipeline_.SDLPipeline());

    ChunkLodUBO ubo{};
    std::memcpy(ubo.vp, vp16, 64);
    ubo.origin_xyz_pad[0] = origin_xyz[0];
    ubo.origin_xyz_pad[1] = origin_xyz[1];
    ubo.origin_xyz_pad[2] = origin_xyz[2];
    ubo.origin_xyz_pad[3] = 0.0f;
    SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));

    SDL_GPUBufferBinding vb{ vbo_.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib{ ibo_.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(rp, index_count_, 1, 0, 0, 0);
}
#endif // MD_SDL_GPU
