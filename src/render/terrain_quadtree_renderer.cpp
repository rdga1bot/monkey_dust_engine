#include <monkey_dust/render/terrain_quadtree_renderer.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/platform/md_log.h>
#include <monkey_dust/world/terrain_quadtree_mesh.h>
#include <cstring>

namespace {
// Field order matches shaders/terrain_quadtree.vert's TerrainQuadtreeUBO
// exactly (std140).
struct TerrainQuadtreeUBO {
    float vp[16];
    float origin_size_texel_morph[4];
    float height_range[4];
    float cam_pos_skirt[4];
};
static_assert(sizeof(TerrainQuadtreeUBO) == 64 + 16 * 3, "TerrainQuadtreeUBO size mismatch");
} // namespace

bool TerrainQuadtreeRenderer::Init(SDL_GPUDevice* /*dev*/) {
    md::TerrainQuadtreeMesh mesh = md::BuildTerrainQuadtreeMesh();

    filled_ibo_.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, mesh.filled_indices.data(),
                      mesh.filled_indices.size() * sizeof(uint32_t));
    skirt_ibo_.Init(0x8893u, mesh.skirt_indices.data(),
                     mesh.skirt_indices.size() * sizeof(uint32_t));
    filled_index_count_ = (uint32_t)mesh.filled_indices.size();
    skirt_index_count_  = (uint32_t)mesh.skirt_indices.size();

    GpuPipeline::Desc pd;
    pd.layout.count       = 0; // vertex-buffer-less -- gl_VertexIndex comes from the bound IBO alone
    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false; // skirt quads face outward on all 4 borders
    pd.has_depth_target   = true;
    pd.vert_uniform_bufs  = 1;
    pd.vert_samplers      = 2; // 2026-08-24: #398 reverted -- heightTex + normalTex (world-wide)
    pd.vert_path = "shaders/terrain_quadtree.vert";
    pd.frag_path = "shaders/terrain_gbuffer_mini.frag"; // unmodified, same output contract
    pd.frag_uniform_bufs = 0;
    pd.frag_samplers     = 0;
    pd.frag_storage_bufs = 0;
    pd.color_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    if (!gbuffer_pipeline_.Create(pd)) {
        MD_LOG(MD_LOG_WARNING, "[TerrainQuadtreeRenderer] pipeline create failed");
        return false;
    }
    ready_ = true;
    return true;
}

void TerrainQuadtreeRenderer::Shutdown(SDL_GPUDevice* /*dev*/) {
    gbuffer_pipeline_.Destroy();
    filled_ibo_.Shutdown();
    skirt_ibo_.Shutdown();
    ready_ = false;
}

void TerrainQuadtreeRenderer::DrawNode(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                        const TerrainWorldHeightmap& hmap, const float* vp16,
                                        const TerrainQuadtree::VisibleNode& node,
                                        float cam_x, float cam_y, float cam_z) {
    if (!ready_) return;

    // texelSize = this node's own world footprint / 16 quads (kPatchQuads,
    // terrain_quadtree_mesh.h) -- matches the shader's own kGridSize=17 decode.
    constexpr float kPatchQuads = 16.0f;
    float texelSize = node.size / kPatchQuads;

    SDL_BindGPUGraphicsPipeline(rp, gbuffer_pipeline_.SDLPipeline());

    TerrainQuadtreeUBO ubo{};
    std::memcpy(ubo.vp, vp16, 64);
    ubo.origin_size_texel_morph[0] = node.origin_x;
    ubo.origin_size_texel_morph[1] = node.origin_z;
    ubo.origin_size_texel_morph[2] = texelSize;
    ubo.origin_size_texel_morph[3] = node.morph;
    ubo.height_range[0] = hmap.HeightMin();
    ubo.height_range[1] = hmap.HeightMax();
    ubo.height_range[2] = hmap.WorldExtent();
    ubo.height_range[3] = (float)hmap.Resolution();
    ubo.cam_pos_skirt[0] = cam_x;
    ubo.cam_pos_skirt[1] = cam_y;
    ubo.cam_pos_skirt[2] = cam_z;
    ubo.cam_pos_skirt[3] = node.skirt_depth;
    SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));

    SDL_GPUTextureSamplerBinding samp[2] = {
        { hmap.Texture(), hmap.Sampler() },
        { hmap.NormalTexture(), hmap.NormalSampler() },
    };
    SDL_BindGPUVertexSamplers(rp, 0, samp, 2);

    SDL_GPUBufferBinding filled_ib{ filled_ibo_.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &filled_ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(rp, filled_index_count_, 1, 0, 0, 0);

    SDL_GPUBufferBinding skirt_ib{ skirt_ibo_.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &skirt_ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(rp, skirt_index_count_, 1, 0, 0, 0);
}
#endif // MD_SDL_GPU
