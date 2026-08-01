#include <monkey_dust/render/terrain_baked_renderer.h>
#ifdef MD_SDL_GPU
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>

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

    // TERRAIN_CA_REBUILD_PROMPT.md Phase 2 §3 -- depth-only early-Z prepass
    // pipeline. Separate vert shader source (terrain_baked_prepass.vert),
    // same rationale as TerrainPatchRenderer's prepass_pipeline_.
    GpuPipeline::Desc pp = pd;
    pp.vert_path      = "shaders/terrain_baked_prepass.vert";
    pp.frag_path      = "shaders/shadow_csm.frag";
    pp.depth_only     = true;
    pp.frag_uniform_bufs = 0;
    pp.frag_samplers     = 0;
    pp.frag_storage_bufs = 0;
    if (!prepass_pipeline_.Create(pp)) {
        fprintf(stderr, "[TerrainBakedRenderer] prepass pipeline create failed\n");
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

    // Phase 3 -- start the background bake worker. Single consumer of
    // async_queue_'s request side, single producer of its result side
    // (mirrors NavSystem's worker exactly, nav_async_queue.h).
    worker_running_.store(true, std::memory_order_release);
    worker_ = std::thread([this]() {
        while (worker_running_.load(std::memory_order_acquire)) {
            TerrainBakeRequest req;
            if (!async_queue_.TryDequeueRequest(req)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            int N = kPatchN >> req.tier;
            TerrainBakeResult result;
            result.tier = req.tier;
            result.patch_key = req.patch_key;
            result.vertex_count = TerrainBake_VertexCount(N);
            TerrainBake_ComputeVertices(req.origin_x, req.origin_z, req.patch_size, N,
                                        req.has_coarser, req.normal_step_m,
                                        req.sample_height, result.verts);
            result.valid = true;
            // Bounded retry, not an infinite spin -- if PumpAsyncResults
            // hasn't drained in a while (main thread stalled/shutting
            // down), drop this result rather than hang the worker
            // forever; the request will simply get re-issued next frame
            // by TryGetOrRequestBakeAsync once it's no longer "pending".
            for (int tries = 0; tries < 100; ++tries) {
                if (async_queue_.TryEnqueueResult(result)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    ready_ = true;
    return true;
}

void TerrainBakedRenderer::Shutdown(SDL_GPUDevice* /*dev*/) {
    // Join (not detach) BEFORE any GPU teardown -- this project already
    // hit a destroy-during-upload race from a detached loader thread
    // once (CLAUDE.md's Hardware Checklist), fixed by joining before
    // GpuDevice::Shutdown(). Same rule applies here even though this
    // worker never touches the GPU directly -- Shutdown() itself
    // destroys vbo_/ibo_ device buffers right below, which a still-
    // running worker has no business racing against either.
    worker_running_.store(false, std::memory_order_release);
    if (worker_.joinable()) worker_.join();

    for (int t = 0; t < kNumTiers; ++t) {
        ibo_[t].Shutdown();
        for (int s = 0; s < kMaxSlotsPerTier; ++s) {
            vbo_[t][s].Shutdown();
            slots_[t][s] = Slot{};
        }
    }
    pipeline_.Destroy();
    prepass_pipeline_.Destroy();
    frame_counter_ = 0;
    touch_counter_ = 0;
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
        slots[hit].last_used_frame = ++touch_counter_;
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
    slots[slot].last_used_frame = ++touch_counter_;
    return slot;
}

bool TerrainBakedRenderer::IsPending(int tier, uint64_t key) {
    for (int i = 0; i < kMaxPending; ++i) {
        if (!pending_[i].used || pending_[i].tier != tier || pending_[i].key != key) continue;
        // task terrain-async-bake-stall: expire a pending entry whose
        // result was silently dropped (worker's bounded retry exhausted) --
        // see this struct's doc comment in the header for the full story.
        // Without this, IsPending stays true forever and the patch never
        // gets re-requested.
        if (frame_counter_ - pending_[i].marked_frame > kPendingTimeoutFrames) {
            pending_[i].used = false;
            return false;
        }
        return true;
    }
    return false;
}

void TerrainBakedRenderer::MarkPending(int tier, uint64_t key) {
    for (int i = 0; i < kMaxPending; ++i) {
        if (!pending_[i].used) {
            pending_[i] = { tier, key, true, frame_counter_ };
            return;
        }
    }
    // Table full -- every slot genuinely in-flight already (bounded by
    // kMaxPending = 2x queue capacity, so this only happens if the
    // worker is badly backed up). Silently skip re-marking; the next
    // frame's IsPending check will still (harmlessly) re-attempt
    // TryEnqueueRequest, which just fails fast if the queue is full too.
}

void TerrainBakedRenderer::ClearPending(int tier, uint64_t key) {
    for (int i = 0; i < kMaxPending; ++i) {
        if (pending_[i].used && pending_[i].tier == tier && pending_[i].key == key) {
            pending_[i].used = false;
            return;
        }
    }
}

int TerrainBakedRenderer::TryGetOrRequestBakeAsync(int tier, uint64_t patch_key,
                                                    float origin_x, float origin_z, float patch_size,
                                                    bool has_coarser, float normal_step_m,
                                                    TerrainHeightSampleFn sample_height) {
    if (!ready_ || tier < 0 || tier >= kNumTiers) return -1;

    Slot* slots = slots_[tier];
    for (int s = 0; s < kMaxSlotsPerTier; ++s) {
        if (slots[s].valid && slots[s].key == patch_key) {
            slots[s].last_used_frame = ++touch_counter_;
            return s;
        }
    }

    if (!IsPending(tier, patch_key)) {
        TerrainBakeRequest req;
        req.tier = tier;
        req.patch_key = patch_key;
        req.origin_x = origin_x; req.origin_z = origin_z; req.patch_size = patch_size;
        req.has_coarser = has_coarser;
        req.normal_step_m = normal_step_m;
        req.sample_height = sample_height;
        req.valid = true;
        if (async_queue_.TryEnqueueRequest(req)) MarkPending(tier, patch_key);
    }
    return -1; // not ready yet -- caller skips drawing this patch this frame
}

void TerrainBakedRenderer::PumpAsyncResults(SDL_GPUCommandBuffer* cmd) {
    if (!ready_) return;
    TerrainBakeResult result;
    // Drain everything ready this frame, not just one -- bounded by the
    // queue's own capacity (8), so this can never loop unboundedly.
    while (async_queue_.TryDequeueResult(result)) {
        if (!result.valid || result.tier < 0 || result.tier >= kNumTiers) continue;
        ClearPending(result.tier, result.patch_key);

        Slot* slots = slots_[result.tier];
        int empty = -1, oldest = 0;
        for (int s = 0; s < kMaxSlotsPerTier; ++s) {
            if (!slots[s].valid && empty < 0) empty = s;
            if (slots[s].last_used_frame < slots[oldest].last_used_frame) oldest = s;
        }
        int slot = empty >= 0 ? empty : oldest;

        void* dst = vbo_[result.tier][slot].MapWrite();
        memcpy(dst, result.verts, (size_t)result.vertex_count * sizeof(TerrainBakedVertex));
        vbo_[result.tier][slot].Unmap();
        vbo_[result.tier][slot].Upload(cmd);

        slots[slot].key             = result.patch_key;
        slots[slot].valid           = true;
        slots[slot].last_used_frame = ++touch_counter_;
    }
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

void TerrainBakedRenderer::DrawSlotDepthOnly(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                              int tier, int slot, const float* vp16,
                                              float origin_x, float origin_z, float patch_size, float lod,
                                              float cam_x, float cam_y, float cam_z) {
    if (!ready_ || tier < 0 || tier >= kNumTiers) return;
    if (slot < 0 || slot >= kMaxSlotsPerTier || !slots_[tier][slot].valid) return;

    SDL_BindGPUGraphicsPipeline(rp, prepass_pipeline_.SDLPipeline());

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
