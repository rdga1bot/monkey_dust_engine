#include <monkey_dust/render/terrain_patch_renderer.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/platform/md_log.h>
#include <cstring>

namespace {
// Field order matches shaders/terrain_patch_novbo.vert's PatchNoVboUBO
// exactly (std140).
struct PatchNoVboUBO {
    float vp[16];
    float patch_origin_world_texel_quads[4];
    float height_range[4];
    float cam_pos_and_skirt[4];
};
} // namespace

bool TerrainPatchRenderer::Init(SDL_GPUDevice* dev) {
    GpuPipeline::Desc pd;
    pd.layout.count       = 0; // vertex-buffer-less -- the whole point of this renderer
    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false; // skirt quads face outward on all 4 borders
    pd.has_depth_target   = true;
    pd.vert_uniform_bufs  = 1;
    pd.vert_samplers      = 2; // heightTex + normalTex (Phase 3 baked normals)
    pd.vert_path = "shaders/terrain_patch_novbo.vert";
    pd.frag_path = "shaders/terrain_gbuffer_mini.frag"; // unmodified, same output contract
    pd.frag_uniform_bufs = 0;
    pd.frag_samplers     = 0;
    pd.frag_storage_bufs = 0;
    pd.color_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    if (!gbuffer_pipeline_.Create(pd)) {
        MD_LOG(MD_LOG_WARNING, "[TerrainPatchRenderer] pipeline create failed");
        return false;
    }
    ready_ = true;
    return true;
}

void TerrainPatchRenderer::Shutdown(SDL_GPUDevice* dev) {
    (void)dev;
    gbuffer_pipeline_.Destroy();
    ready_ = false;
}

void TerrainPatchRenderer::DrawPatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                      const TerrainWorldHeightmap& hmap, const float* vp16,
                                      float origin_x, float origin_z, int tier, float skirt_depth_m,
                                      float cam_x, float cam_y, float cam_z) {
    if (!ready_) return;

    int quadsPerEdge = QuadsPerEdgeForTier(tier);
    // texel size = this patch's own world footprint / quadsPerEdge. The
    // caller's TerrainPatchGrid.PatchSize() is the patch's fixed world
    // footprint regardless of tier -- coarser tiers cover the SAME
    // footprint with fewer, larger quads, exactly like the old system's
    // per-tier static meshes did. 300m matches TerrainPatchGrid's own
    // recommended patch_size (serene-pondering-teapot.md's Phase 4 doc).
    constexpr float kPatchFootprintM = 300.0f;
    float texelSize = kPatchFootprintM / (float)quadsPerEdge;

    SDL_BindGPUGraphicsPipeline(rp, gbuffer_pipeline_.SDLPipeline());

    PatchNoVboUBO ubo{};
    std::memcpy(ubo.vp, vp16, 64);
    ubo.patch_origin_world_texel_quads[0] = origin_x;
    ubo.patch_origin_world_texel_quads[1] = origin_z;
    ubo.patch_origin_world_texel_quads[2] = texelSize;
    ubo.patch_origin_world_texel_quads[3] = (float)quadsPerEdge;
    ubo.height_range[0] = hmap.HeightMin();
    ubo.height_range[1] = hmap.HeightMax();
    ubo.height_range[2] = hmap.WorldExtent();
    ubo.height_range[3] = (float)hmap.Resolution();
    ubo.cam_pos_and_skirt[0] = cam_x;
    ubo.cam_pos_and_skirt[1] = cam_y;
    ubo.cam_pos_and_skirt[2] = cam_z;
    ubo.cam_pos_and_skirt[3] = skirt_depth_m;
    SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));

    SDL_GPUTextureSamplerBinding samplers[2] = {
        { hmap.Texture(), hmap.Sampler() },
        { hmap.NormalTexture(), hmap.NormalSampler() },
    };
    SDL_BindGPUVertexSamplers(rp, 0, samplers, 2);

    uint32_t surfaceVerts = (uint32_t)(quadsPerEdge * quadsPerEdge) * 6u;
    uint32_t skirtVerts    = (uint32_t)quadsPerEdge * 6u * 4u;
    SDL_DrawGPUPrimitives(rp, surfaceVerts + skirtVerts, 1, 0, 0);
}
#endif
