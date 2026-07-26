#include <monkey_dust/render/terrain_quadtree_renderer.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/job_system.h>
#include <SDL3/SDL_timer.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <chrono>

// Vertex-stage constant buffer — MUST mirror QuadVert in
// shaders/slang/terrain_quadtree.slang field-for-field (same byte-offset
// discipline as TerrainVertUBO/TerrainFwdVert, see terrain_renderer.h).
struct QuadVertUBO {
    float vp[16];              // 64B, float4x4
    float origin_x;            // float4 slot: origin_x,origin_z,size_m,height_min_m
    float origin_z;
    float size_m;
    float height_min_m;
    float height_max_m;        // float4 slot: height_max_m,region_origin_x,region_origin_z,region_size
    float region_origin_x;
    float region_origin_z;
    float region_size;
    float morph_t;             // float4 slot: morph_t,grid_n,texel_step_m,pad
    float grid_n;
    float texel_step_m;        // world-metre spacing between height-texture texels (normal reconstruction)
    float _pad1;
    float cam_pos_ws[4];       // xyz=camera world position (vDist/fog), w=unused
};
static_assert(sizeof(QuadVertUBO) == 128, "QuadVertUBO size mismatch");

// Fragment UBO — mirrors terrain_quadtree.slang's QuadFrag byte-for-byte.
// Deliberately smaller than TerrainRenderer::TerrainFragUBO: this shader
// only ever runs the "zone-lookup" shading branch (see Draw()'s doc
// comment), so ground_layers_a/b, blend_layers (comboAlternates gate) and
// use_zone_lookup (a runtime toggle between two branches this shader never
// has) don't apply here.
struct QuadFragUBO {
    float sun_dir_str[4];     // xyz=dir, w=strength
    float ambient[4];         // xyz=colour, w=unused
    float world_params[4];    // xy=origin_xz, z=world_to_uv, w=unused
    float fog_color_near[4];  // xyz=fog colour, w=fog_near
    float fog_far;
    float _pad[3];
};
static_assert(sizeof(QuadFragUBO) == 80, "QuadFragUBO size mismatch");

bool TerrainQuadtreeRenderer::BuildUnitPatchMesh() {
    constexpr int N  = kPatchN;
    constexpr int VC = (N + 1) * (N + 1);
    constexpr int IC = N * N * 6;
    static float    verts[VC * 2];
    static uint32_t idx[IC];

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
    patch_vbo_.Init(0x8892u /*GL_ARRAY_BUFFER*/, verts, sizeof(verts));
    patch_ibo_.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, idx, sizeof(idx));
    patch_idx_count_ = (uint32_t)IC;
    return true;
}

// Region texture SUBSAMPLE resolution: matches TerrainAtlas's real
// resolution exactly (TERRAIN_GRID=128 steps/zone, terrain_chunk.h) — was
// 64 (half-resolution) until 2026-07-26's bug #3 investigation ("character
// floating above terrain"): the region heightmap texture only fed the
// vertex-shader mesh at 64 steps/zone (7.2m spacing) while the player's
// rendered Y (TerrainQuery::GetHeight -> TerrainAtlas_SampleWorld) reads
// the full 128-step/zone (3.6m) atlas directly — on rugged terrain the
// coarser mesh's bilinear interpolation between its own samples could miss
// a real atlas feature by a measured worst case of 19.35m (avg 0.43m,
// measured via a temporary subsample-error scan over this region), visually
// a player standing over a local dip/rise the mesh smoothed away. Now equal
// to TERRAIN_GRID, so kAtlasStepScale below is always 1 (kept, not
// simplified away, so a future TERRAIN_GRID change doesn't silently
// reintroduce the col/row scale bug the constant was introduced to fix —
// see its own doc comment). kMaxRes below was raised to match (16-zone
// editor window: 16*128+1=2049, was 16*64+1=1025).
static constexpr int kZoneGridStep64 = TERRAIN_GRID;
// Real-atlas steps covered by one kZoneGridStep64 subsample step. MUST use
// TerrainAtlas_GetHeight's real col/row range (0..TERRAIN_GRID) — passing a
// kZoneGridStep64-space index directly to it, unscaled, was a real bug
// (2026-07-26, quadtree-LOD terrain rewrite Phase 7 investigation): with
// scale=1 back when kZoneGridStep64 was 64 (half TERRAIN_GRID), a
// "boundary" between two 64-subsample zone blocks landed at REAL atlas col
// 63 of the first zone (its own MIDPOINT, since a real zone spans 0..128)
// spliced directly against col 0 of the NEXT zone — two unrelated points in
// the actual terrain, producing large, essentially random height
// discontinuities (confirmed via TerrainQuadtreeRenderer::UploadHeightmapRegion's
// adjacent-sample scan: deltas up to 157.8m over one region-sample step) that
// showed up as near-vertical spike walls in the real geometry. See
// CLAUDE_STATE.md for the full investigation.
static constexpr int kAtlasStepScale = TERRAIN_GRID / kZoneGridStep64; // 128/128 = 1

// Maps a global region-grid sample index (0..zone_span*kZoneGridStep64) to
// (zone index, local col/row 0..kZoneGridStep64, in kZoneGridStep64
// SUBSAMPLE units — caller must scale by kAtlasStepScale before passing to
// TerrainAtlas_GetHeight) — zones share their boundary vertex (zone Z's
// col=kZoneGridStep64 == zone Z+1's col=0), so only the very last global
// sample needs the "clamp into the last zone's last col" special case.
static void s_region_sample_to_zone(int gi, int zone_span, int& zi, int& local) {
    zi    = gi / kZoneGridStep64;
    local = gi % kZoneGridStep64;
    if (zi >= zone_span) { zi = zone_span - 1; local = kZoneGridStep64; }
}

// Pure-CPU sampling + RGBA packing — shared between the synchronous
// UploadHeightmapRegion (used once by Init(), where blocking at startup is
// already accepted, see scene_render.cpp's SDL_WaitForGPUIdle comment) and
// KickRebuildAsync's JobSystem worker. MUST NOT touch any GPU resource —
// JobSystem's own contract (job_system.h): "jobs must not touch... GPU
// resources", enforced here structurally by simply never calling into
// gpu_hal from this function. h_tmp/rgba must each be at least
// kMaxRes*kMaxRes (resp. *4) floats/bytes — callers own that storage
// (TerrainQuadtreeRenderer::h_tmp_/rgba_ in practice) so two concurrent
// callers on the SAME instance would corrupt each other's buffer; callers
// are responsible for their own single-flight guard (rebuild_in_flight_
// for the async path, or simply never called concurrently for the sync
// Init() path).
bool TerrainQuadtreeRenderer::s_sample_and_pack(int zx0, int zy0, int zone_span,
                                                 float* h_tmp, uint8_t* rgba,
                                                 int& out_N, float& out_hmin, float& out_hmax,
                                                 float& out_texel_step_m) {
    const int N = zone_span * kZoneGridStep64 + 1;
    if (N > kMaxRes) {
        fprintf(stderr, "[TerrainQuadtreeRenderer] region too large: zone_span=%d -> %dx%d samples (cap %d)\n",
                zone_span, N, N, kMaxRes);
        return false;
    }
    float hmin = 1e9f, hmax = -1e9f;
    for (int row = 0; row < N; ++row) {
        int zzi, zlocal_row;
        s_region_sample_to_zone(row, zone_span, zzi, zlocal_row);
        for (int col = 0; col < N; ++col) {
            int zxi, zlocal_col;
            s_region_sample_to_zone(col, zone_span, zxi, zlocal_col);
            // *kAtlasStepScale: convert kZoneGridStep64-space local col/row
            // into TerrainAtlas_GetHeight's real 0..TERRAIN_GRID range — see
            // kAtlasStepScale's doc comment for the bug this fixes.
            float h = TerrainAtlas_GetHeight(zx0 + zxi, zy0 + zzi,
                                              zlocal_col * kAtlasStepScale, zlocal_row * kAtlasStepScale);
            h_tmp[row * N + col] = h;
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }
    float range = hmax - hmin;
    if (range < 1.f) range = 1.f; // avoid div-by-zero on a perfectly flat region

    // Pack uint16 height into R(low byte)+G(high byte) of an RGBA8 texture
    // (see header doc comment — this HAL has no R16 GPU format yet).
    for (int i = 0; i < N * N; ++i) {
        float    norm = (h_tmp[i] - hmin) / range;
        uint32_t h16  = (uint32_t)(norm * 65535.f + 0.5f);
        if (h16 > 65535u) h16 = 65535u;
        rgba[i * 4 + 0] = (uint8_t)(h16 & 0xFFu);
        rgba[i * 4 + 1] = (uint8_t)((h16 >> 8) & 0xFFu);
        rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 255;
    }

    out_N            = N;
    out_hmin         = hmin;
    out_hmax         = hmin + range;
    out_texel_step_m = ((float)zone_span * CHUNK_SIZE) / (float)(N - 1);
    return true;
}

bool TerrainQuadtreeRenderer::UploadHeightmapRegion(int zx0, int zy0, int zone_span,
                                                     float& out_height_min, float& out_height_max) {
    auto t0 = std::chrono::steady_clock::now();
    int N = 0;
    if (!s_sample_and_pack(zx0, zy0, zone_span, h_tmp_, rgba_, N,
                            out_height_min, out_height_max, region_texel_step_m_))
        return false;
    auto t1 = std::chrono::steady_clock::now();

    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = false;
    sd.flip_v     = false;

    GpuTexture tex;
    if (!tex.InitFromMemory(rgba_, N, N, sd)) return false;
    height_tex_     = tex.TakeSDLTexture();
    height_sampler_ = tex.TakeSDLSampler();
    auto t2 = std::chrono::steady_clock::now();
    if (getenv("MD_QT_DEBUG")) {
        auto ms = [](auto a, auto b){ return std::chrono::duration<double,std::milli>(b-a).count(); };
        fprintf(stderr, "[QTDBG] UploadHeightmapRegion(sync) N=%d sample+pack=%.2fms gpu_upload=%.2fms total=%.2fms\n",
                N, ms(t0,t1), ms(t1,t2), ms(t0,t2));
    }
    return true;
}

void TerrainQuadtreeRenderer::s_run_rebuild_job(void* p) {
    RebuildJobArgs* a = (RebuildJobArgs*)p;
    a->out_ok = s_sample_and_pack(a->zx0, a->zy0, a->zone_span, a->h_tmp, a->rgba,
                                   a->out_N, a->out_hmin, a->out_hmax, a->out_texel_step_m);
}

bool TerrainQuadtreeRenderer::KickRebuildAsync(int zx0, int zy0, int zone_span,
                                                float local_origin_x, float local_origin_z) {
    if (!ready_) return false;
    if (SDL_GetAtomicInt(&rebuild_in_flight_) > 0) return false; // previous job still running

    rebuild_job_args_.zx0             = zx0;
    rebuild_job_args_.zy0             = zy0;
    rebuild_job_args_.zone_span       = zone_span;
    rebuild_job_args_.local_origin_x  = local_origin_x;
    rebuild_job_args_.local_origin_z  = local_origin_z;
    rebuild_job_args_.h_tmp           = h_tmp_;
    rebuild_job_args_.rgba            = rgba_;
    rebuild_job_args_.out_ok          = false;

    JobSystem::Get().Submit(s_run_rebuild_job, &rebuild_job_args_, &rebuild_in_flight_);
    rebuild_has_pending_result_ = true;
    return true;
}

bool TerrainQuadtreeRenderer::RebuildInFlight() const {
    return SDL_GetAtomicInt(&rebuild_in_flight_) > 0;
}

bool TerrainQuadtreeRenderer::PollRebuildApply(float& out_height_min, float& out_height_max) {
    if (!rebuild_has_pending_result_) return false;
    if (SDL_GetAtomicInt(&rebuild_in_flight_) > 0) return false; // still running
    rebuild_has_pending_result_ = false;
    if (!rebuild_job_args_.out_ok) return false;

    auto t0 = std::chrono::steady_clock::now();
    GpuSamplerDesc sd;
    sd.min_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.mag_filter = GpuSamplerDesc::Filter::LINEAR;
    sd.wrap_s     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.wrap_t     = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    sd.gen_mipmap = false;
    sd.flip_v     = false;

    GpuTexture tex;
    if (!tex.InitFromMemory(rebuild_job_args_.rgba, rebuild_job_args_.out_N, rebuild_job_args_.out_N, sd))
        return false;

    // Old texture stays live (still what Draw() has been using every frame
    // this job was in flight) right up until this point — only released
    // once the replacement is fully ready, so there is never a frame where
    // height_tex_ is null/half-built.
    if (height_tex_)     { SDL_ReleaseGPUTexture(md::GpuDevice::Get().SDLDevice(), height_tex_); }
    if (height_sampler_) { SDL_ReleaseGPUSampler(md::GpuDevice::Get().SDLDevice(), height_sampler_); }
    height_tex_     = tex.TakeSDLTexture();
    height_sampler_ = tex.TakeSDLSampler();

    region_size_          = (float)rebuild_job_args_.zone_span * CHUNK_SIZE;
    region_origin_x_      = rebuild_job_args_.local_origin_x;
    region_origin_z_      = rebuild_job_args_.local_origin_z;
    region_texel_step_m_  = rebuild_job_args_.out_texel_step_m;
    out_height_min        = rebuild_job_args_.out_hmin;
    out_height_max        = rebuild_job_args_.out_hmax;

    if (getenv("MD_QT_DEBUG")) {
        auto t1 = std::chrono::steady_clock::now();
        fprintf(stderr, "[QTDBG] PollRebuildApply N=%d gpu_upload=%.2fms (sample+pack ran off-thread)\n",
                rebuild_job_args_.out_N,
                std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return true;
}

bool TerrainQuadtreeRenderer::Init(int zx0, int zy0, int zone_span,
                                    float local_origin_x, float local_origin_z,
                                    float& out_height_min, float& out_height_max) {
    GpuPipeline::Desc pd;
    pd.vert_path = "shaders/terrain_quadtree.vert";
    pd.frag_path = "shaders/terrain_quadtree.frag";

    pd.layout.count      = 1;
    pd.layout.stride     = 8; // float2 aUV
    pd.layout.attribs[0] = { 0, 0, GpuAttribFmt::F2 };

    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = false;
    pd.has_depth_target   = true;

    pd.vert_uniform_bufs = 1; // slot 0: QuadVertUBO
    pd.vert_samplers     = 1; // heightTex — VTF, confirmed safe 2026-07-25
    pd.frag_uniform_bufs = 1; // slot 0: QuadFragUBO
    // b0=kenshi colour overlay, b1=ground DDS array, b2=grass/dirt/road
    // mask — the same 3 "zone-lookup path" samplers terrain_forward.slang's
    // fsMain uses (BORROWED from the caller's TerrainRenderer, see Draw()'s
    // doc comment — this class never loads its own copy).
    pd.frag_samplers     = 3;
    // b3 (right after the 3 samplers, SDL_GPU set=2 contiguous layout):
    // per-zone ground-layer lookup, same BORROWED SSBO.
    pd.frag_storage_bufs = 1;

    if (!pipeline_.Create(pd)) {
        fprintf(stderr, "[TerrainQuadtreeRenderer] pipeline create failed\n");
        return false;
    }
    if (!BuildUnitPatchMesh()) return false;
    if (!UploadHeightmapRegion(zx0, zy0, zone_span, out_height_min, out_height_max)) return false;

    region_size_     = (float)zone_span * CHUNK_SIZE;
    region_origin_x_ = local_origin_x;
    region_origin_z_ = local_origin_z;

    SDL_SetAtomicInt(&rebuild_in_flight_, 0);
    rebuild_has_pending_result_ = false;

    ready_ = true;
    return true;
}

bool TerrainQuadtreeRenderer::RebuildRegion(int zx0, int zy0, int zone_span,
                                             float local_origin_x, float local_origin_z,
                                             float& out_height_min, float& out_height_max) {
    if (!ready_) return false;
    if (height_tex_)     { SDL_ReleaseGPUTexture(md::GpuDevice::Get().SDLDevice(), height_tex_); height_tex_ = nullptr; }
    if (height_sampler_) { SDL_ReleaseGPUSampler(md::GpuDevice::Get().SDLDevice(), height_sampler_); height_sampler_ = nullptr; }
    if (!UploadHeightmapRegion(zx0, zy0, zone_span, out_height_min, out_height_max)) {
        ready_ = false;
        return false;
    }
    region_size_     = (float)zone_span * CHUNK_SIZE;
    region_origin_x_ = local_origin_x;
    region_origin_z_ = local_origin_z;
    return true;
}

void TerrainQuadtreeRenderer::Shutdown() {
    // A KickRebuildAsync job writes into THIS instance's h_tmp_/rgba_ scratch
    // members from a worker thread — must not still be running when this
    // object (and that memory) goes away. Spin-wait rather than a global
    // JobSystem::Flush() (which would also wait on unrelated in-flight work,
    // e.g. animation/pathing jobs from other systems) since rebuild_in_flight_
    // is this instance's own counter.
    while (SDL_GetAtomicInt(&rebuild_in_flight_) > 0) { SDL_Delay(1); }
    if (height_tex_)     { SDL_ReleaseGPUTexture(md::GpuDevice::Get().SDLDevice(), height_tex_); height_tex_ = nullptr; }
    if (height_sampler_) { SDL_ReleaseGPUSampler(md::GpuDevice::Get().SDLDevice(), height_sampler_); height_sampler_ = nullptr; }
    patch_vbo_.Shutdown();
    patch_ibo_.Shutdown();
    pipeline_.Destroy();
    ready_ = false;
}

float TerrainQuadtreeRenderer::ComputeMorphT(float dist, int depth, const float* lod_distances) {
    float near_d = lod_distances[depth];
    float far_d  = (depth == 0) ? (lod_distances[0] * 2.f) : lod_distances[depth - 1];
    if (far_d <= near_d) return 0.f; // degenerate config — never morph rather than divide by ~0
    float t = (dist - near_d) / (far_d - near_d);
    return std::max(0.f, std::min(1.f, t));
}

void TerrainQuadtreeRenderer::Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                                    const float* vp16,
                                    float origin_x, float origin_z, float size_m,
                                    float morph_t,
                                    float height_min_m, float height_max_m,
                                    const TerrainRenderer::SunParams& sun,
                                    float cam_x, float cam_y, float cam_z,
                                    float world_origin_x, float world_origin_z, float world_to_uv,
                                    float fog_far, const float fog_color[3], float fog_near,
                                    const TerrainRenderer& ground) {
    if (!ready_ || !height_tex_ || !height_sampler_) return;

    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb{ patch_vbo_.SDLBuffer(), 0u };
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib{ patch_ibo_.SDLBuffer(), 0u };
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    QuadVertUBO vubo{};
    memcpy(vubo.vp, vp16, 64);
    vubo.origin_x       = origin_x;
    vubo.origin_z       = origin_z;
    vubo.size_m         = size_m;
    vubo.height_min_m   = height_min_m;
    vubo.height_max_m   = height_max_m;
    vubo.region_origin_x = region_origin_x_;
    vubo.region_origin_z = region_origin_z_;
    vubo.region_size     = region_size_;
    vubo.morph_t        = morph_t;
    vubo.grid_n         = (float)kPatchN;
    vubo.texel_step_m   = region_texel_step_m_;
    vubo.cam_pos_ws[0] = cam_x; vubo.cam_pos_ws[1] = cam_y;
    vubo.cam_pos_ws[2] = cam_z; vubo.cam_pos_ws[3] = 0.f;
    SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

    SDL_GPUTextureSamplerBinding vtsb{ height_tex_, height_sampler_ };
    SDL_BindGPUVertexSamplers(rp, 0, &vtsb, 1);

    QuadFragUBO fubo{};
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

    SDL_GPUTextureSamplerBinding bindings[3];
    ground.GetSharedGroundSamplers(bindings);
    if (!bindings[0].texture || !bindings[0].sampler) return;
    SDL_BindGPUFragmentSamplers(rp, 0, bindings, 3);

    SDL_GPUBuffer* sbuf = ground.ZoneGroundLayersSSBO();
    SDL_BindGPUFragmentStorageBuffers(rp, 0, &sbuf, 1);

    SDL_DrawGPUIndexedPrimitives(rp, patch_idx_count_, 1, 0, 0, 0);
}
#endif // MD_SDL_GPU
