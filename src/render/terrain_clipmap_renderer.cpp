#include <monkey_dust/render/terrain_clipmap_renderer.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/world/terrain_clipmap_mesh.h>
#include <cstdio>
#include <cstring>

namespace {
// Field order matches shaders/terrain_clipmap_gbuffer.vert's
// ClipmapVertUBO exactly (std140).
struct ClipmapVertUBO {
    float vp[16];
    float origin_index_texel_cache[4];
    float mesh_params[4];
    float height_range[4];
    float cam_pos_ws[4];
};
static_assert(sizeof(ClipmapVertUBO) == 64 + 16 * 4, "ClipmapVertUBO size mismatch");

// Same 4-side-plane (no far plane) AABB test the old TerrainPatchGrid::
// SelectVisible used (engine git history, commit 65859fe, pre-Phase-8) --
// borrowed approach, not the deleted class itself.
bool AabbInFrustum(float ox, float oz, float size, float ymin, float ymax, const float fp[16]) {
    for (int p = 0; p < 4; ++p) {
        const float* pl = fp + p * 4;
        float px = (pl[0] >= 0.f) ? (ox + size) : ox;
        float pz = (pl[2] >= 0.f) ? (oz + size) : oz;
        float py = (pl[1] >= 0.f) ? ymax : ymin;
        if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.f) return false;
    }
    return true;
}
} // namespace

bool TerrainClipmapRenderer::Init(SDL_GPUDevice* /*dev*/) {
    md::ClipmapMesh mesh = md::BuildClipmapMesh(TerrainClipmapCache::kMeshQuads, kTileQuads);

    vbo_.Init(0x8892u /*GL_ARRAY_BUFFER*/, mesh.vertices.data(),
              mesh.vertices.size() * sizeof(md::ClipmapMeshVertex));
    filled_ibo_.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, mesh.filled_indices.data(),
                      mesh.filled_indices.size() * sizeof(uint32_t));
    ring_ibo_.Init(0x8893u, mesh.ring_indices.data(),
                   mesh.ring_indices.size() * sizeof(uint32_t));
    filled_index_count_ = (uint32_t)mesh.filled_indices.size();
    ring_index_count_   = (uint32_t)mesh.ring_indices.size();

    num_tiles_ = (int)mesh.tiles.size();
    if (num_tiles_ > kMaxTiles) num_tiles_ = kMaxTiles; // can't happen at kTileQuads=16, defensive
    for (int i = 0; i < num_tiles_; ++i) tiles_[i] = mesh.tiles[i];

    GpuPipeline::Desc gb;
    gb.layout.count      = 1;
    gb.layout.stride     = 8; // vec2 aUV
    gb.layout.attribs[0] = { 0, 0, GpuAttribFmt::F2 };

    gb.raster.depth_test  = true;
    gb.raster.depth_write = true;
    gb.raster.cull_back   = false;
    gb.has_depth_target   = true;

    gb.vert_uniform_bufs = 1;
    gb.vert_samplers     = 1; // cacheTex, VTF (same safe pattern as terrain_patch_gbuffer.vert)

    gb.vert_path = "shaders/terrain_clipmap_gbuffer.vert";
    gb.frag_path = "shaders/terrain_gbuffer_mini.frag"; // UNMODIFIED, shared with the old system
    gb.frag_uniform_bufs = 0;
    gb.frag_samplers     = 0;
    gb.frag_storage_bufs = 0;
    gb.color_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;

    if (!gbuffer_pipeline_.Create(gb)) {
        fprintf(stderr, "[TerrainClipmapRenderer] gbuffer pipeline create failed\n");
        return false;
    }

    ready_ = true;
    return true;
}

void TerrainClipmapRenderer::Shutdown(SDL_GPUDevice* /*dev*/) {
    gbuffer_pipeline_.Destroy();
    vbo_.Shutdown();
    filled_ibo_.Shutdown();
    ring_ibo_.Shutdown();
    ready_ = false;
}

void TerrainClipmapRenderer::DrawLevel(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int level,
                                        const TerrainClipmapCache& cache, const float* vp16,
                                        float cam_x, float cam_y, float cam_z,
                                        float height_min_m, float height_max_m,
                                        const float frustum_planes[16]) {
    if (!ready_ || !cache.IsReady()) return;
    if (level < 0 || level >= TerrainClipmapCache::kNumLevels) return;

    SDL_BindGPUGraphicsPipeline(rp, gbuffer_pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb{ vbo_.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);

    // Level 0 (finest, no finer level inside it) draws the SOLID grid;
    // every other level draws the "ring" (hole excluded, already covered
    // by the next-finer level) -- see terrain_clipmap_mesh.h's doc comment.
    bool use_filled = (level == 0);
    GpuStaticBuffer& ibo = use_filled ? filled_ibo_ : ring_ibo_;
    SDL_GPUBufferBinding ib{ ibo.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    int origin_x, origin_z;
    cache.LevelOriginIndex(level, origin_x, origin_z);
    float texel_size = cache.TexelSize(level);

    ClipmapVertUBO vubo{};
    memcpy(vubo.vp, vp16, 64);
    vubo.origin_index_texel_cache[0] = (float)origin_x;
    vubo.origin_index_texel_cache[1] = (float)origin_z;
    vubo.origin_index_texel_cache[2] = texel_size;
    vubo.origin_index_texel_cache[3] = (float)TerrainClipmapCache::kCacheTexels;
    vubo.mesh_params[0] = (float)TerrainClipmapCache::kMeshQuads;
    vubo.height_range[0] = height_min_m;
    vubo.height_range[1] = height_max_m;
    vubo.cam_pos_ws[0] = cam_x; vubo.cam_pos_ws[1] = cam_y; vubo.cam_pos_ws[2] = cam_z;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    SDL_GPUTextureSamplerBinding vtsb{ cache.CacheTexture(level), cache.CacheSampler(level) };
    SDL_BindGPUVertexSamplers(rp, 0, &vtsb, 1);

    // Per-tile frustum culling (minimal-variant Part B): one direct draw
    // per VISIBLE tile, using that tile's {first_index, index_count}
    // sub-range of the SAME bound IBO -- skips tiles outside the frustum
    // entirely instead of always drawing the level's full footprint.
    // Direct draws (not indirect) per this project's own measured
    // precedent on this HW/driver (task #380).
    float tile_world_size = (float)kTileQuads * texel_size;
    for (int i = 0; i < num_tiles_; ++i) {
        const md::ClipmapTileRange& t = tiles_[i];
        uint32_t first_index = use_filled ? t.filled_first_index : t.ring_first_index;
        uint32_t index_count = use_filled ? t.filled_index_count : t.ring_index_count;
        if (index_count == 0) continue; // tile fully inside the ring's hole

        float tile_ox = ((float)origin_x + (float)(t.tile_col * kTileQuads)) * texel_size;
        float tile_oz = ((float)origin_z + (float)(t.tile_row * kTileQuads)) * texel_size;
        if (!AabbInFrustum(tile_ox, tile_oz, tile_world_size, height_min_m, height_max_m, frustum_planes)) {
            continue;
        }

        SDL_DrawGPUIndexedPrimitives(rp, index_count, 1, first_index, 0, 0);
    }
}
#endif // MD_SDL_GPU
