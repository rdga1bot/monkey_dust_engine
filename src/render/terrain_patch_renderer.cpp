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

bool TerrainPatchRenderer::BuildTierMesh(int tier, int quads_per_edge) {
    const int N = quads_per_edge;
    const int SURF_VC = (N + 1) * (N + 1);
    const int SURF_IC = N * N * 6;
    // task terrain-ca-rebuild skirt-fix (2026-08-01): cross-tier
    // T-junction fix. Adjacent patches drawn at DIFFERENT discrete LOD
    // tiers sample DIFFERENT mip levels of the same world heightmap at
    // their shared boundary (terrain_patch.vert's tierLod = floor(lod)
    // selects the mip) -- genuinely different height VALUES at nominally
    // shared edge positions, not just a vertex-density mismatch. The
    // whole-patch tier clamp (this renderer's own Granite-reference
    // approach, see class header) keeps neighbor tiers within 1 step of
    // each other but does NOT make their sampled heights bit-identical --
    // residual crack confirmed via magenta-sky-override A/B
    // (terrain_research/PROGRESS.md 2026-07-31: 145/245/1184 residual px
    // after the tierLod mip-consistency fix, described there as "a real
    // cross-tier T-junction crack, needs real stitching or skirt
    // geometry"). Fix: a vertical skirt wall along all 4 patch edges,
    // pushed straight down by SKIRT_DEPTH_M in the vertex shader -- hides
    // any residual height mismatch behind opaque geometry instead of
    // trying to make two different mip samples agree (the same class of
    // fix this codebase already used for the OLD TerrainQuadtree's
    // chunk-border gaps, BuildLodIboStitched). Skirt vertices share the
    // exact same (u,v) as their surface-edge counterpart -- they sample
    // the identical height/mip in the vertex shader and only differ by
    // the shader-applied vertical offset, so they're always attached,
    // never a horizontal gap of their own.
    const int SKIRT_VC = 4 * (N + 1);
    const int SKIRT_IC = 4 * N * 6;
    const int VC = SURF_VC + SKIRT_VC;
    const int IC = SURF_IC + SKIRT_IC;
    // 3 floats/vertex: aUV.xy + aSkirt (0=surface, 1=pushed down in VS).
    static constexpr int kMaxVC = (kPatchN + 1) * (kPatchN + 1) + 4 * (kPatchN + 1);
    static constexpr int kMaxIC = kPatchN * kPatchN * 6 + 4 * kPatchN * 6;
    static float    verts[kMaxVC * 3];
    static uint32_t idx[kMaxIC];

    for (int row = 0; row <= N; ++row) {
        for (int col = 0; col <= N; ++col) {
            int vi = row * (N + 1) + col;
            verts[vi * 3 + 0] = (float)col / (float)N;
            verts[vi * 3 + 1] = (float)row / (float)N;
            verts[vi * 3 + 2] = 0.0f;
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

    // 4 independent edge strips (north/south/west/east) -- each gets its
    // own (N+1) new skirt vertices appended after the surface block, plus
    // N quads connecting consecutive surface-edge vertices to their
    // skirt-vertex counterparts. Corners get two overlapping skirt quads
    // (one per adjoining edge) -- harmless (both drop to the same depth).
    auto edgeSurfIdx = [N](int e, int i) -> int {
        switch (e) {
            case 0: return 0 * (N + 1) + i;   // north, row=0
            case 1: return N * (N + 1) + i;   // south, row=N
            case 2: return i * (N + 1) + 0;   // west,  col=0
            default: return i * (N + 1) + N;  // east,  col=N
        }
    };
    const int skirtBase = SURF_VC;
    for (int e = 0; e < 4; ++e) {
        const int base = skirtBase + e * (N + 1);
        for (int i = 0; i <= N; ++i) {
            int surf = edgeSurfIdx(e, i);
            verts[(base + i) * 3 + 0] = verts[surf * 3 + 0];
            verts[(base + i) * 3 + 1] = verts[surf * 3 + 1];
            verts[(base + i) * 3 + 2] = 1.0f;
        }
        for (int i = 0; i < N; ++i) {
            uint32_t s0 = (uint32_t)edgeSurfIdx(e, i);
            uint32_t s1 = (uint32_t)edgeSurfIdx(e, i + 1);
            uint32_t k0 = (uint32_t)(base + i);
            uint32_t k1 = (uint32_t)(base + i + 1);
            idx[ii++] = s0; idx[ii++] = k0; idx[ii++] = s1;
            idx[ii++] = s1; idx[ii++] = k0; idx[ii++] = k1;
        }
    }

    tier_vbo_[tier].Init(0x8892u /*GL_ARRAY_BUFFER*/, verts, (size_t)VC * 3 * sizeof(float));
    tier_ibo_[tier].Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, idx, (size_t)IC * sizeof(uint32_t));
    tier_idx_count_[tier] = (uint32_t)IC;
    return true;
}

bool TerrainPatchRenderer::Init(SDL_GPUDevice* /*dev*/) {
    // pd is a template shared by the prepass/gbuffer pipelines below (Desc
    // copy) for the vertex layout/raster state they all have in common --
    // its own vert_path/frag_path are never used to create a pipeline
    // directly (the forward-shading pipeline that used to fill them,
    // DrawBatch's, was removed -- see terrain_research/
    // ARCHITECTURE_AUDIT_2026_08_04.md).
    GpuPipeline::Desc pd;
    pd.layout.count      = 1;
    pd.layout.stride     = 12; // float3: aUV.xy + aSkirt (0=surface, 1=skirt)
    pd.layout.attribs[0] = { 0, 0, GpuAttribFmt::F3 };

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

    // TERRAIN_CA_REBUILD_PROMPT.md Phase 2 §3 -- depth-only early-Z prepass
    // pipeline. Same vertex layout as pd above (must match tier_vbo_/
    // inst_vbo_'s real buffer contents), but a SEPARATE vert shader source
    // file (terrain_patch_prepass.vert, not terrain_patch.vert reused) --
    // see that file's header comment for why (Intel Gen9 ANV cross-pipeline
    // vertex codegen divergence, same class of bug already fixed for NPCs
    // via animated_prepass.vert). shadow_csm.frag is the existing generic
    // empty depth-only fragment shader (also used by the NPC prepass and
    // ShadowSystem's cascades).
    GpuPipeline::Desc pp = pd;
    pp.vert_path      = "shaders/terrain_patch_prepass.vert";
    pp.frag_path      = "shaders/shadow_csm.frag";
    pp.depth_only     = true;
    pp.frag_uniform_bufs = 0;
    pp.frag_samplers     = 0;
    pp.frag_storage_bufs = 0;
    if (!prepass_pipeline_.Create(pp)) {
        fprintf(stderr, "[TerrainPatchRenderer] prepass pipeline create failed\n");
        return false;
    }

    // TERRAIN_CA_REBUILD_PROMPT.md Phase 4 -- Variant A G-buffer pipeline.
    // Own dedicated vertex shader (terrain_patch_gbuffer.vert, byte-for-
    // byte copy of terrain_patch.vert) -- see that file's header comment
    // for why (Intel Gen9 ANV cross-pipeline vertex codegen divergence,
    // same class of bug already fixed for the depth prepass below and,
    // originally, NPCs via animated_prepass.vert). Found live 2026-08-04:
    // reusing terrain_patch.vert's compiled module here (the previous
    // approach, on the theory that a dedicated depth target made the
    // known cross-pipeline risk inapplicable) produced a G-buffer whose W
    // channel (packed normal) varied correctly per-pixel/per-camera-move
    // but whose XYZ channels (world position) read back CONSTANT
    // regardless of screen position or a live 3000m camera move --
    // despite both being written by the SAME single vec4 store in
    // terrain_gbuffer_mini.frag, ruling out a data-flow bug and matching
    // this exact known GPU/driver-codegen bug class instead. color_format
    // overridden to RGBA32F -- world-space positions (up to tens of
    // thousands of metres) need full float range, R8G8B8A8_UNORM's
    // normalized [0,1] range cannot represent them.
    GpuPipeline::Desc gb = pd;
    gb.vert_path         = "shaders/terrain_patch_gbuffer.vert";
    gb.frag_path         = "shaders/terrain_gbuffer_mini.frag";
    gb.frag_uniform_bufs = 0;
    gb.frag_samplers     = 0;
    gb.frag_storage_bufs = 0;
    gb.color_format       = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    if (!gbuffer_pipeline_.Create(gb)) {
        fprintf(stderr, "[TerrainPatchRenderer] gbuffer pipeline create failed\n");
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
    prepass_pipeline_.Destroy();
    gbuffer_pipeline_.Destroy();
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

void TerrainPatchRenderer::DrawBatchDepthOnly(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int tier,
                                               const float* vp16, float patch_size,
                                               const TerrainWorldHeightmap& hmap,
                                               float cam_x, float cam_y, float cam_z) {
    if (!ready_ || !hmap.IsReady()) return;
    if (tier < 0 || tier >= kNumTiers) return;
    if (inst_count_[tier] == 0) return;

    SDL_BindGPUGraphicsPipeline(rp, prepass_pipeline_.SDLPipeline());

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
    vubo.world_origin_x = 0.f;
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

    SDL_DrawGPUIndexedPrimitives(rp, tier_idx_count_[tier], inst_count_[tier], 0, 0, 0);
}

void TerrainPatchRenderer::DrawBatchGBuffer(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, int tier,
                                             const float* vp16, float patch_size,
                                             const TerrainWorldHeightmap& hmap,
                                             float cam_x, float cam_y, float cam_z) {
    if (!ready_ || !hmap.IsReady()) return;
    if (tier < 0 || tier >= kNumTiers) return;
    if (inst_count_[tier] == 0) return;

    SDL_BindGPUGraphicsPipeline(rp, gbuffer_pipeline_.SDLPipeline());

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
    vubo.world_origin_x = 0.f;
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

    SDL_DrawGPUIndexedPrimitives(rp, tier_idx_count_[tier], inst_count_[tier], 0, 0, 0);
}
#endif // MD_SDL_GPU
