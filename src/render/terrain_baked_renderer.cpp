#include <monkey_dust/render/terrain_baked_renderer.h>
#ifdef MD_SDL_GPU
#include <cstdio>
#include <cstring>
#include <vector>

// Vertex-stage constant buffer -- MUST mirror shaders/terrain_baked.vert's
// PatchVert field-for-field. One draw call per patch, so everything
// TerrainPatchRenderer's Instance stream carried per-instance is simply
// per-draw here instead -- no heightmap-texture params needed at all
// (baked heights are already real world-space metres).
struct BakedPatchVertUBO {
    float vp[16];
    float origin_x, origin_z, patch_size, lod;
    float cam_pos_ws[4];
};
static_assert(sizeof(BakedPatchVertUBO) == 96, "BakedPatchVertUBO size mismatch");

// Reuses TerrainPatchRenderer's own PatchFragUBO layout exactly (same
// fragment shader, terrain_patch.frag, unchanged) -- redeclared here
// rather than shared across TUs to keep this renderer independently
// buildable/removable (see terrain_baked_renderer.h's top comment).
struct BakedPatchFragUBO {
    float sun_dir_str[4];
    float ambient[4];
    float world_params[4];
    float fog_color_near[4];
    float fog_far;
    float _pad[3];
};
static_assert(sizeof(BakedPatchFragUBO) == 80, "BakedPatchFragUBO size mismatch");

bool TerrainBakedRenderer::Init(SDL_GPUDevice* /*dev*/) {
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_baked.vert";
    pd.frag_path = "shaders/terrain_patch.frag"; // reused UNCHANGED, see terrain_baked.vert's doc comment

    pd.layout.count      = 1;
    pd.layout.stride     = sizeof(TerrainBakedVertex); // 32B: uv(8)+heights(8)+skirt(4)+normal(12)
    pd.layout.attribs[0] = { 0, offsetof(TerrainBakedVertex, u),             GpuAttribFmt::F2 };
    pd.layout.attribs[1] = { 1, offsetof(TerrainBakedVertex, height_fine),   GpuAttribFmt::F2 };
    pd.layout.attribs[2] = { 2, offsetof(TerrainBakedVertex, skirt),         GpuAttribFmt::F1 };
    pd.layout.attribs[3] = { 3, offsetof(TerrainBakedVertex, normal_x),      GpuAttribFmt::F3 };

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;
    pd.vert_samplers     = 0; // no heightTex -- the whole point of baked
    pd.frag_uniform_bufs = 1;
    pd.frag_samplers     = 4; // same 4 ground samplers terrain_patch.frag reads
    pd.frag_storage_bufs = 1; // zoneGroundLayers

    if (!pipeline_.Create(pd)) {
        fprintf(stderr, "[TerrainBakedRenderer] pipeline create failed\n");
        return false;
    }

    int quads = kPatchN;
    for (int t = 0; t < kNumTiers; ++t) {
        int ic = TerrainBake_IndexCount(quads);
        std::vector<uint32_t> idx((size_t)ic);
        TerrainBake_ComputeIndices(quads, idx.data());
        ibo_[t].Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, idx.data(), (size_t)ic * sizeof(uint32_t));
        idx_count_[t] = (uint32_t)ic;

        int vc = TerrainBake_VertexCount(quads);
        for (int s = 0; s < kMaxSlotsPerTier; ++s)
            vbo_[t][s].Init((uint32_t)vc, sizeof(TerrainBakedVertex));

        quads = quads > 1 ? quads / 2 : 1;
    }

    ready_ = true;
    return true;
}

void TerrainBakedRenderer::Shutdown(SDL_GPUDevice* /*dev*/) {
    for (int t = 0; t < kNumTiers; ++t) {
        ibo_[t].Shutdown();
        for (int s = 0; s < kMaxSlotsPerTier; ++s) {
            vbo_[t][s].Shutdown();
            slots_[t][s] = Slot{};
        }
    }
    pipeline_.Destroy();
    frame_counter_ = 0;
    ready_ = false;
}

int TerrainBakedRenderer::GetOrBakePatch(SDL_GPUCommandBuffer* cmd, int tier, uint64_t patch_key,
                                          float origin_x, float origin_z, float patch_size,
                                          bool has_coarser, float normal_step_m,
                                          TerrainHeightSampleFn sample_height) {
    if (!ready_ || tier < 0 || tier >= kNumTiers) return -1;

    Slot* slots = slots_[tier];
    int hit = -1, empty = -1, oldest = 0;
    for (int s = 0; s < kMaxSlotsPerTier; ++s) {
        if (slots[s].valid && slots[s].key == patch_key) { hit = s; break; }
        if (!slots[s].valid && empty < 0) empty = s;
        if (slots[s].last_used_frame < slots[oldest].last_used_frame) oldest = s;
    }

    if (hit >= 0) {
        slots[hit].last_used_frame = frame_counter_;
        return hit;
    }

    int slot = empty >= 0 ? empty : oldest;
    int N = kPatchN >> tier;
    int vc = TerrainBake_VertexCount(N);
    std::vector<TerrainBakedVertex> verts((size_t)vc);
    TerrainBake_ComputeVertices(origin_x, origin_z, patch_size, N, has_coarser,
                                normal_step_m, sample_height, verts.data());

    void* dst = vbo_[tier][slot].MapWrite();
    memcpy(dst, verts.data(), (size_t)vc * sizeof(TerrainBakedVertex));
    vbo_[tier][slot].Unmap();
    vbo_[tier][slot].Upload(cmd);

    slots[slot].key             = patch_key;
    slots[slot].valid           = true;
    slots[slot].last_used_frame = frame_counter_;
    return slot;
}

void TerrainBakedRenderer::DrawSlot(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                     int tier, int slot, const float* vp16,
                                     float origin_x, float origin_z, float patch_size, float lod,
                                     float cam_x, float cam_y, float cam_z,
                                     const TerrainRenderer::SunParams& sun,
                                     float world_origin_x, float world_origin_z, float world_to_uv,
                                     float fog_far, const float fog_color[3], float fog_near,
                                     const TerrainRenderer& ground) {
    if (!ready_ || tier < 0 || tier >= kNumTiers) return;
    if (slot < 0 || slot >= kMaxSlotsPerTier || !slots_[tier][slot].valid) return;

    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb{ vbo_[tier][slot].SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib{ ibo_[tier].SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    BakedPatchVertUBO vubo{};
    memcpy(vubo.vp, vp16, 64);
    vubo.origin_x = origin_x;
    vubo.origin_z = origin_z;
    vubo.patch_size = patch_size;
    vubo.lod = lod;
    vubo.cam_pos_ws[0] = cam_x; vubo.cam_pos_ws[1] = cam_y;
    vubo.cam_pos_ws[2] = cam_z; vubo.cam_pos_ws[3] = 0.f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    BakedPatchFragUBO fubo{};
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0] = world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2] = world_to_uv;    fubo.world_params[3] = 0.f;
    fubo.fog_color_near[0] = fog_color[0]; fubo.fog_color_near[1] = fog_color[1];
    fubo.fog_color_near[2] = fog_color[2]; fubo.fog_color_near[3] = fog_near;
    fubo.fog_far = fog_far;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

    SDL_GPUTextureSamplerBinding bindings[4];
    ground.GetSharedGroundSamplers(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 4);

    SDL_GPUBuffer* sbuf = ground.ZoneGroundLayersSSBO();
    SDL_BindGPUFragmentStorageBuffers(rp, 0, &sbuf, 1);

    SDL_DrawGPUIndexedPrimitives(rp, idx_count_[tier], 1, 0, 0, 0);
}

int TerrainBakedRenderer::ResidentSlotCount(int tier) const {
    if (tier < 0 || tier >= kNumTiers) return 0;
    int n = 0;
    for (int s = 0; s < kMaxSlotsPerTier; ++s)
        if (slots_[tier][s].valid) ++n;
    return n;
}
#endif // MD_SDL_GPU
