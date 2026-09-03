#ifdef MD_SDL_GPU
#include <monkey_dust/render/terrain_shading_projected.h>
#include <monkey_dust/render/gpu_device.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

// Mirrors terrain_patch_renderer.cpp's PatchFragUBO / terrain_baked_renderer.
// cpp's BakedPatchFragUBO -- each pipeline file keeps its own copy of these
// small POD UBO structs rather than sharing one header (established
// convention in this codebase, see terrain_baked_renderer.cpp).
struct ProjFragUBO {
    float sun_dir_str[4];
    float ambient[4];
    float world_params[4];
    float fog_color_near[4];
    float fog_far;
    float _pad[3];
};
static_assert(sizeof(ProjFragUBO) == 80, "ProjFragUBO size mismatch");

struct ProjCamUBO {
    float cam_pos_ws[4];
};
static_assert(sizeof(ProjCamUBO) == 16, "ProjCamUBO size mismatch");

bool TerrainShadingProjected::CreateTextures(int w, int h) {
    w_ = w; h_ = h;

    GpuSamplerDesc gs;
    gs.min_filter = GpuSamplerDesc::Filter::NEAREST;
    gs.mag_filter = GpuSamplerDesc::Filter::NEAREST;
    gs.wrap_s = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    gs.wrap_t = GpuSamplerDesc::Wrap::CLAMP_TO_EDGE;
    if (!gbuf_color_.InitRenderTarget(w, h, gs, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT)) {
        MD_LOG(MD_LOG_WARNING, "[TerrainShadingProjected] gbuf_color_ create failed");
        return false;
    }
    gbuf_depth_.Init(w, h, /*shadow_border=*/false);
    if (!gbuf_depth_.SDLTexture()) {
        MD_LOG(MD_LOG_WARNING, "[TerrainShadingProjected] gbuf_depth_ create failed");
        return false;
    }
    return true;
}

bool TerrainShadingProjected::Init(md::GpuDeviceHandle dev, int w, int h) {
    if (!CreateTextures(w, h)) return false;

    GpuPipeline::Desc rd;
    rd.vert_path = "shaders/terrain_shading_screenspace.vert";
    rd.frag_path = "shaders/terrain_shading_screenspace.frag";
    rd.layout.count  = 0;
    rd.layout.stride = 0;
    rd.raster.depth_test      = true;
    rd.raster.depth_write     = false;
    rd.raster.cull_back       = false;
    rd.raster.depth_compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    rd.has_depth_target   = true;
    rd.vert_uniform_bufs  = 0;
    rd.vert_samplers      = 0;
    rd.frag_uniform_bufs  = 2;  // set=3 binding=0 ProjFragUBO, binding=1 ProjCamUBO
    rd.frag_samplers      = 10;  // set=2: tex_colour,tex_ground,tex_ground_baked,tex_overlay_mask,tex_ground_nml (task #12); gbufPacked,gbufDepth; terrain-vt Phase 4: vtIndirection,vtAtlas; zoneGroundLayersTex (texture, not SSBO, since 2026-08-09)
    rd.frag_storage_bufs  = 1;  // set=2 binding=10: vtPageMeta (terrain-vt clipmap fix)
    if (!resolve_pipeline_.Create(rd)) {
        MD_LOG(MD_LOG_WARNING, "[TerrainShadingProjected] resolve pipeline create failed");
        return false;
    }

    ready_ = true;
    MD_LOG(MD_LOG_INFO, "[TerrainShadingProjected] ready %dx%d (RGBA32F gbuffer + isolated D32_FLOAT)", w, h);
    return true;
}

// Only the two render-target textures depend on window size -- resolve_pipeline_
// depends solely on texture FORMAT (unchanged across a resize), so it must never
// be touched here. The original version of this function called the full Init()
// (destroying+recreating the pipeline too), which meant every resize destroyed a
// GPU pipeline object that a still-in-flight command buffer from the previous
// frame could still reference -- a real SIGSEGV inside the Intel Vulkan driver,
// diagnosed live 2026-08-02 even after relocating the call to before
// AcquireCommandBuffer (that relocation alone only fixed the FIRST-frame case).
void TerrainShadingProjected::EnsureSize(md::GpuDeviceHandle dev, int w, int h) {
    (void)dev;
    if (!ready_ || (w == w_ && h == h_) || w <= 0 || h <= 0) return;
    gbuf_depth_.Shutdown();
    gbuf_color_.Shutdown();
    CreateTextures(w, h);
}

void TerrainShadingProjected::Shutdown() {
    resolve_pipeline_.Destroy();
    gbuf_depth_.Shutdown();
    gbuf_color_.Shutdown();
    ready_ = false;
}

SDL_GPURenderPass* TerrainShadingProjected::BeginGBufferPass(md::GpuCommandBufferHandle cmd) {
    if (!ready_) return nullptr;

    // clear_color MUST be set explicitly to {0,0,0,0} -- ColorPassDesc's own
    // default is {0,0,0,1} (alpha=1), which would silently corrupt this
    // G-buffer's alpha channel (same class of bug caught in smaa_system.cpp;
    // see docs/HAL_CLOSURE_PROGRESS.md's "documented lesson" on this default).
    GpuCommandBuffer cb;
    GpuCommandBuffer::ColorPassDesc cpd;
    cpd.cmd            = cmd;
    cpd.color_tex[0]      = gbuf_color_.SDLTexture();
    cpd.depth_tex      = gbuf_depth_.SDLTexture();
    cpd.clear_color[0] = 0.f; cpd.clear_color[1] = 0.f;
    cpd.clear_color[2] = 0.f; cpd.clear_color[3] = 0.f;
    cpd.clear_depth    = 1.f;
    cpd.load_color     = false; // CLEAR
    cpd.load_depth     = false; // CLEAR
    cb.BeginColorPass(cpd);
    return cb.SDLPass();
}

void TerrainShadingProjected::EndGBufferPass() {
    // Caller keeps the SDL_GPURenderPass* returned by BeginGBufferPass to
    // draw with -- ending it is a plain SDL call, no state kept here (same
    // pattern as GBuffer::End, which also doesn't need internal pass_
    // tracking since the caller already holds the pointer). Intentionally
    // a no-op body: callers call SDL_EndGPURenderPass(rp) themselves, this
    // exists only so BeginGBufferPass/EndGBufferPass read as a matched pair
    // at call sites (mirrors GBuffer::Begin/End's shape without duplicating
    // its internal pass_ member for a pass this class doesn't otherwise
    // need to remember between calls).
}

void TerrainShadingProjected::DrawShadingResolve(SDL_GPURenderPass* rp, md::GpuCommandBufferHandle cmd,
                                                   const TerrainRenderer::SunParams& sun,
                                                   float cam_x, float cam_y, float cam_z,
                                                   float world_origin_x, float world_origin_z, float world_to_uv,
                                                   float fog_far, const float fog_color[3], float fog_near,
                                                   const TerrainRenderer& ground, const TerrainVtPageCache& vt,
                                                   bool shade_constant_debug) {
    if (!ready_) return;
    // terrain-vt Phase 4: defensive -- the atlas/indirection textures must
    // be valid, non-null objects to bind (SDL_GPU requires a real sampler
    // binding, not an optional one).
    //
    // 2026-08-27 fix: this USED to check `!vt.IsReady()`, which bailed out
    // of this entire function -- including the SDL_DrawGPUPrimitives call
    // below that actually resolves terrain shading to the screen -- any
    // time real VT caching was disabled (IsReady() reflects "actively
    // caching pages", not "safe to bind"). Since VT is now permanently
    // disabled (see TerrainVtPageCache::Init()'s own doc comment),
    // IsReady() is permanently false, and terrain silently stopped
    // rendering entirely (G-buffer fill still ran and cost real GPU time;
    // this resolve pass, the thing that turns it into visible pixels,
    // never did). Root-caused via git bisect + live screenshot
    // classification. TerrainVtPageCache::InitDisabledFallback() now
    // guarantees Atlas/IndirectionTexture()/PageMetaSSBO() are always
    // valid 1x1/1-slot objects even when disabled, so a direct null check
    // on what's actually bound below is both correct AND matches this
    // comment's original intent (defensive null-safety, not a "VT active"
    // gate).
    if (!vt.AtlasTexture() || !vt.IndirectionTexture() || !vt.PageMetaSSBO()) return;

    GpuPassView pv = GpuPassView::FromRaw(rp, cmd);
    pv.BindPipeline(&resolve_pipeline_);

    ProjFragUBO fubo{};
    fubo.sun_dir_str[0] = sun.dir[0]; fubo.sun_dir_str[1] = sun.dir[1];
    fubo.sun_dir_str[2] = sun.dir[2]; fubo.sun_dir_str[3] = sun.strength;
    fubo.ambient[0]     = sun.ambient[0]; fubo.ambient[1] = sun.ambient[1];
    fubo.ambient[2]     = sun.ambient[2]; fubo.ambient[3] = 0.f;
    fubo.world_params[0] = world_origin_x; fubo.world_params[1] = world_origin_z;
    fubo.world_params[2] = world_to_uv;
    fubo.world_params[3] = shade_constant_debug ? 1.f : 0.f;  // Крок 0 ablation, see header doc comment
    fubo.fog_color_near[0] = fog_color[0]; fubo.fog_color_near[1] = fog_color[1];
    fubo.fog_color_near[2] = fog_color[2]; fubo.fog_color_near[3] = fog_near;
    fubo.fog_far = fog_far;
    GpuPushFragmentUniforms(cmd, 0, &fubo, sizeof(fubo));

    ProjCamUBO cubo{};
    cubo.cam_pos_ws[0] = cam_x; cubo.cam_pos_ws[1] = cam_y;
    cubo.cam_pos_ws[2] = cam_z; cubo.cam_pos_ws[3] = 0.f;
    GpuPushFragmentUniforms(cmd, 1, &cubo, sizeof(cubo));

    // set=2: same 5 shared ground samplers the normal forward terrain draw
    // binds (TerrainPatchRenderer::DrawBatch) -- index 4 (tex_ground_nml)
    // added task #12 (2026-09-03).
    SDL_GPUTextureSamplerBinding ground_bindings[5];
    ground.GetSharedGroundSamplers(ground_bindings);
    // All 5 must be checked, not just index 0 -- unlike slots 0/2/3 (plain
    // sampler2D, always backed by a same-typed 1x1 fallback texture even
    // when their real asset fails to load), slots 1/4 (tex_ground_array,
    // tex_ground_nml_array, both sampler2DArray in the shader) have no
    // fallback of a matching image type in FillSamplerBindings and fall
    // back to nullptr/nullptr.
    for (int i = 0; i < 5; ++i) {
        if (!ground_bindings[i].texture || !ground_bindings[i].sampler) return;
    }
    pv.BindFragmentSamplers(0, ground_bindings, 5);
    // Single remaining SSBO (vtPageMeta) -- zoneGroundLayers moved to a
    // texture (binding=9, bound below with the other samplers) since
    // 2026-08-09, see ZoneGroundLayersTexture's header doc comment.
    SDL_GPUBuffer* storage_bufs[1] = { vt.PageMetaSSBO() };
    pv.BindFragmentStorageBuffers(0, storage_bufs, 1);

    // set=1: this class's own G-buffer (packed world-pos/normal + dedicated depth).
    SDL_GPUTextureSamplerBinding gbuf_bindings[2] = {
        { gbuf_color_.SDLTexture(), gbuf_color_.SDLSampler() },
        { gbuf_depth_.SDLTexture(), gbuf_depth_.SDLSampler() },
    };
    pv.BindFragmentSamplers(5, gbuf_bindings, 2);

    // terrain-vt Phase 4: indirection + physical atlas -- bindings 7/8,
    // continuing the same contiguous sampler run (see terrain_shading_
    // screenspace.frag's own doc comment on why the SSBO above must stay
    // numbered AFTER every sampler in this set).
    SDL_GPUTextureSamplerBinding vt_bindings[2] = {
        { vt.IndirectionTexture(), vt.IndirectionSampler() },
        { vt.AtlasTexture(),       vt.AtlasSampler() },
    };
    pv.BindFragmentSamplers(7, vt_bindings, 2);

    // Zone ground-layer lookup -- binding=9, texture not SSBO since
    // 2026-08-09 (Filament-blocker reduction, see ZoneGroundLayersTexture's
    // header doc comment). Continues the same contiguous sampler run.
    SDL_GPUTextureSamplerBinding zone_binding[1] = {
        { ground.ZoneGroundLayersTexture(), ground.ZoneGroundLayersSampler() },
    };
    pv.BindFragmentSamplers(9, zone_binding, 1);

    pv.Draw(3, 1, 0, 0);
}
#endif
