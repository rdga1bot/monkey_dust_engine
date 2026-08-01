#include <monkey_dust/render/terrain_patch_renderer.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <cstdio>
#include <algorithm>

// Vertex-stage constant buffer — MUST mirror PatchVert in
// shaders/slang/terrain_patch.slang field-for-field. Phase 5: per-patch
// fields (origin/lod/neighbor_tier_n) moved to the per-instance vertex
// stream (Instance, below) — this UBO now holds only what's constant
// across an entire tier's batched instanced draw.
struct PatchVertUBO {
    float vp[16];
    float patch_size, tier_n, world_origin_x, world_origin_z;
    // res_texels: TerrainWorldHeightmap::Resolution() (texel count/axis) --
    // needed in the shader to correct the vertex-grid UV (worldX/world_extent,
    // vertex i at i/(res_texels-1)) into the half-texel-centered UV a GPU
    // sampler actually reads a texture at (texel i centered at (i+0.5)/
    // res_texels). Without this, SampleLevel silently samples up to half a
    // texel away from the intended world position -- confirmed via a real
    // --exec repro (camera placed at TerrainQuery's exact reported ground
    // height ended up embedded inside solid rendered terrain) that this is
    // large enough to fully bury a character on steep local slopes, even
    // though the per-axis error is under one texel (~3.6m) everywhere.
    float world_extent, height_min_m, height_max_m, res_texels;
    float cam_pos_ws[4];
};
static_assert(sizeof(PatchVertUBO) == 112, "PatchVertUBO size mismatch");

struct PatchFragUBO {
    float sun_dir_str[4];
    float ambient[4];
    float world_params[4];
    float fog_color_near[4];
    float fog_far;
    float _pad[3];
};
static_assert(sizeof(PatchFragUBO) == 80, "PatchFragUBO size mismatch");

bool TerrainPatchRenderer::BuildTierMesh(int tier, int quads_per_edge) {
    const int N = quads_per_edge;
    const int VC = (N + 1) * (N + 1);
    const int IC = N * N * 6;
    // 2 floats/vertex: aUV.xy only -- no edge-mask (see header comment on
    // why per-vertex edge snapping was replaced with a whole-patch tier
    // clamp, matching the real Granite reference).
    static float    verts[(kPatchN + 1) * (kPatchN + 1) * 2];
    static uint32_t idx[kPatchN * kPatchN * 6];

    for (int row = 0; row <= N; ++row) {
        for (int col = 0; col <= N; ++col) {
            int vi = row * (N + 1) + col;
            verts[vi * 2 + 0] = (float)col / (float)N;
            verts[vi * 2 + 1] = (float)row / (float)N;
        }
    }
    int ii = 0;
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            uint32_t v00 = (uint32_t)(row * (N + 1) + col);
            uint32_t v10 = v00 + 1;
            uint32_t v01 = v00 + (uint32_t)(N + 1);
            uint32_t v11 = v01 + 1;
            idx[ii++] = v00; idx[ii++] = v01; idx[ii++] = v10;
            idx[ii++] = v10; idx[ii++] = v01; idx[ii++] = v11;
        }
    }
    tier_vbo_[tier].Init(0x8892u /*GL_ARRAY_BUFFER*/, verts, (size_t)VC * 2 * sizeof(float));
    tier_ibo_[tier].Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, idx, (size_t)IC * sizeof(uint32_t));
    tier_idx_count_[tier] = (uint32_t)IC;
    return true;
}

bool TerrainPatchRenderer::Init(SDL_GPUDevice* /*dev*/) {
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_patch.vert";
    pd.frag_path = "shaders/terrain_patch.frag";

    pd.layout.count      = 1;
    pd.layout.stride     = 8; // float2 aUV
    pd.layout.attribs[0] = { 0, 0, GpuAttribFmt::F2 };

    // Per-instance stream (slot=1, INSTANCE rate): origin.xy + own
    // (already neighbor-clamped) lod -- see Instance struct / header
    // comment. No per-edge neighbor data needed: the CPU-side clamp in
    // SceneRender::UpdateGraniteTerrain already bumps a patch to its
    // coarsest neighbor's tier before it ever reaches here.
    pd.layout.inst_count      = 2;
    pd.layout.inst_stride     = 12;
    pd.layout.inst_per_vertex = false;
    pd.layout.inst_attribs[0] = { 2, 0, GpuAttribFmt::F2 }; // aInstOrigin
    pd.layout.inst_attribs[1] = { 3, 8, GpuAttribFmt::F1 }; // aInstLod

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1;
    pd.vert_samplers     = 1; // heightTex — VTF, confirmed safe 2026-07-25
    pd.frag_uniform_bufs = 1;
    pd.frag_samplers     = 4; // tex_colour, tex_ground array, tex_ground_baked, tex_overlay_mask
    pd.frag_storage_bufs = 1; // zoneGroundLayers

    if (!pipeline_.Create(pd)) {
        fprintf(stderr, "[TerrainPatchRenderer] pipeline create failed\n");
        return false;
    }

    int quads = kPatchN;
    for (int t = 0; t < kNumTiers; ++t) {
        if (!BuildTierMesh(t, quads)) return false;
        quads = std::max(1, quads / 2);
        inst_vbo_[t].Init(kMaxInstancesPerTier, sizeof(Instance));
    }

    ready_ = true;
    return true;
}

void TerrainPatchRenderer::Shutdown(SDL_GPUDevice* /*dev*/) {
    for (int t = 0; t < kNumTiers; ++t) {
        tier_vbo_[t].Shutdown();
        tier_ibo_[t].Shutdown();
        inst_vbo_[t].Shutdown();
        inst_count_[t] = 0;
    }
    pipeline_.Destroy();
    ready_ = false;
}

void TerrainPatchRenderer::UploadInstances(SDL_GPUCommandBuffer* cmd,
                                            const Instance* const insts[kNumTiers],
                                            const int counts[kNumTiers]) {
    if (!ready_) return;
    for (int t = 0; t < kNumTiers; ++t) {
        int n = counts[t];
        if (n <= 0 || !insts[t]) { inst_count_[t] = 0; continue; }
        if (n > kMaxInstancesPerTier) n = kMaxInstancesPerTier;
        void* dst = inst_vbo_[t].MapWrite();
        memcpy(dst, insts[t], (size_t)n * sizeof(Instance));
        inst_vbo_[t].Unmap();
        inst_vbo_[t].Upload(cmd);
        inst_count_[t] = (uint32_t)n;
    }
}

void TerrainPatchRenderer::DrawBatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int tier,
                                      const float* vp16, float patch_size,
                                      const TerrainWorldHeightmap& hmap,
                                      const TerrainRenderer::SunParams& sun,
                                      float cam_x, float cam_y, float cam_z,
                                      float world_origin_x, float world_origin_z, float world_to_uv,
                                      float fog_far, const float fog_color[3], float fog_near,
                                      const TerrainRenderer& ground) {
    if (!ready_ || !hmap.IsReady()) return;
    if (tier < 0 || tier >= kNumTiers) return;
    if (inst_count_[tier] == 0) return;

    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb{ tier_vbo_[tier].SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding instvb{ inst_vbo_[tier].SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 1, &instvb, 1);
    SDL_GPUBufferBinding ib{ tier_ibo_[tier].SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    PatchVertUBO vubo{};
    memcpy(vubo.vp, vp16, 64);
    vubo.patch_size = patch_size;
    vubo.tier_n = (float)TierN(tier);
    vubo.world_origin_x = 0.f; // TerrainWorldHeightmap covers [0, world_extent) both axes
    vubo.world_origin_z = 0.f;
    vubo.world_extent   = hmap.WorldExtent();
    vubo.height_min_m   = hmap.HeightMin();
    vubo.height_max_m   = hmap.HeightMax();
    vubo.res_texels     = (float)hmap.Resolution();
    vubo.cam_pos_ws[0] = cam_x; vubo.cam_pos_ws[1] = cam_y;
    vubo.cam_pos_ws[2] = cam_z; vubo.cam_pos_ws[3] = 0.f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    SDL_GPUTextureSamplerBinding vtsb{ hmap.Texture(), hmap.Sampler() };
    SDL_BindGPUVertexSamplers(rp, 0, &vtsb, 1);

    PatchFragUBO fubo{};
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

    SDL_DrawGPUIndexedPrimitives(rp, tier_idx_count_[tier], inst_count_[tier], 0, 0, 0);
}
#endif // MD_SDL_GPU
