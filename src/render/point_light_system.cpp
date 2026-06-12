#ifdef MD_SDL_GPU
#include <monkey_dust/render/point_light_system.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/platform/md_log.h>
#include <cmath>
#include <cstring>

// ── Fragment / vertex UBO layouts (must match GLSL std140) ──────────────────

struct PointLightVertUBO {
    float mvp[16];              // 64B mat4
};
static_assert(sizeof(PointLightVertUBO) == 64);

struct PointLightFragUBO {
    float light_pos[3]; float radius;    // vec4 @  0
    float light_col[3]; float intensity; // vec4 @ 16
    float cam_pos[3];   float _pad0;     // vec4 @ 32
    float scr_w, scr_h; float _pad1[2]; // vec4 @ 48
    float inv_vp[16];                    // mat4 @ 64
};
static_assert(sizeof(PointLightFragUBO) == 128);

// ── Icosphere generation (1 subdivision: 80 tris × 3 verts = 240 positions) ─

static void NormVec3(float x, float y, float z, float out[3]) {
    float l = sqrtf(x*x + y*y + z*z);
    out[0] = x/l; out[1] = y/l; out[2] = z/l;
}

static void GenIcosphereFlat(float* out, int& vert_count) {
    static const float PHI = 1.6180339887f;
    float base[12][3] = {
        {-1, PHI, 0}, { 1, PHI, 0}, {-1,-PHI, 0}, { 1,-PHI, 0},
        { 0,-1, PHI}, { 0, 1, PHI}, { 0,-1,-PHI}, { 0, 1,-PHI},
        { PHI, 0,-1}, { PHI, 0, 1}, {-PHI, 0,-1}, {-PHI, 0, 1}
    };
    static const int faces[20][3] = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };

    float vn[12][3];
    for (int i = 0; i < 12; ++i)
        NormVec3(base[i][0], base[i][1], base[i][2], vn[i]);

    // Midpoint cache (at most 30 unique edges)
    struct { uint32_t key; float v[3]; } cache[64];
    int cn = 0;

    auto midpt = [&](int a, int b, float out3[3]) {
        uint32_t lo = (uint32_t)(a < b ? a : b);
        uint32_t hi = (uint32_t)(a < b ? b : a);
        uint32_t key = (lo << 16) | hi;
        for (int i = 0; i < cn; ++i) {
            if (cache[i].key == key) {
                out3[0] = cache[i].v[0]; out3[1] = cache[i].v[1]; out3[2] = cache[i].v[2];
                return;
            }
        }
        float mx = (vn[a][0]+vn[b][0])*0.5f;
        float my = (vn[a][1]+vn[b][1])*0.5f;
        float mz = (vn[a][2]+vn[b][2])*0.5f;
        NormVec3(mx, my, mz, out3);
        if (cn < 64) {
            cache[cn].key = key;
            cache[cn].v[0] = out3[0]; cache[cn].v[1] = out3[1]; cache[cn].v[2] = out3[2];
            ++cn;
        }
    };

    auto emit = [&](const float a[3], const float b[3], const float c[3]) {
        float* dst = out + vert_count * 3;
        dst[0]=a[0]; dst[1]=a[1]; dst[2]=a[2];
        dst[3]=b[0]; dst[4]=b[1]; dst[5]=b[2];
        dst[6]=c[0]; dst[7]=c[1]; dst[8]=c[2];
        vert_count += 3;
    };

    vert_count = 0;
    for (int f = 0; f < 20; ++f) {
        int ai = faces[f][0], bi = faces[f][1], ci = faces[f][2];
        float ab[3], bc[3], ca[3];
        midpt(ai, bi, ab);
        midpt(bi, ci, bc);
        midpt(ci, ai, ca);
        emit(vn[ai], ab,    ca);
        emit(ab,     vn[bi], bc);
        emit(ca,     bc,    vn[ci]);
        emit(ab,     bc,    ca);
    }
    // vert_count == 240
}

// ── Column-major mat4 multiply: R = A * B ────────────────────────────────────

static void Mat4Mul(float R[16], const float A[16], const float B[16]) {
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k)
                s += A[k*4+row] * B[col*4+k];
            R[col*4+row] = s;
        }
}

// Build scale+translate model matrix for a sphere volume (column-major)
static void SphereMat4(float M[16], const float pos[3], float r) {
    memset(M, 0, 64);
    M[0]  = r;    // col0.x
    M[5]  = r;    // col1.y
    M[10] = r;    // col2.z
    M[12] = pos[0]; M[13] = pos[1]; M[14] = pos[2]; // col3 = translation
    M[15] = 1.f;
}

// ── PointLightSystem ─────────────────────────────────────────────────────────

namespace md {

PointLightSystem& PointLightSystem::Get() {
    static PointLightSystem inst;
    return inst;
}

void PointLightSystem::Init(SDL_GPUDevice* dev) {
    if (!RenderTierSystem::Get().IsDeferred()) {
        MD_LOG(MD_LOG_INFO, "PointLightSystem: Forward tier — disabled");
        return;
    }
    dev_ = dev;

    // Generate flat (non-indexed) icosphere vertices
    static float sphere_data[240 * 3];
    GenIcosphereFlat(sphere_data, sphere_vert_count_);

    sphere_verts_.Init(0x8892 /*GL_ARRAY_BUFFER*/, sphere_data,
                       (uint32_t)(sphere_vert_count_ * 3 * sizeof(float)));

    // Additive blend pipeline — no depth attachment (depth sampled as texture)
    GpuPipeline::Desc d;
    d.vert_path = "shaders/deferred_point.vert";
    d.frag_path = "shaders/deferred_point.frag";
    d.layout.count      = 1;
    d.layout.attribs[0] = { 0, 0, GpuAttribFmt::F3 };
    d.layout.stride     = sizeof(float) * 3;
    d.raster.blend_enable = true;
    d.raster.src_factor   = GpuBlendFactor::ONE;
    d.raster.dst_factor   = GpuBlendFactor::ONE;   // additive
    d.raster.depth_test   = false;
    d.raster.depth_write  = false;
    d.raster.cull_back    = false;
    d.vert_uniform_bufs   = 1;   // set=1,binding=0 mvp
    d.frag_uniform_bufs   = 1;   // set=1,binding=0 PointLightFragUBO
    d.frag_samplers       = 3;   // RT0, RT1, Depth
    d.has_depth_target    = false;
    d.depth_only          = false;

    if (!pipeline_.Create(d)) {
        MD_LOG(MD_LOG_WARNING, "PointLightSystem: pipeline create failed");
        return;
    }

    enabled_ = true;
    MD_LOG(MD_LOG_INFO, "PointLightSystem: ready (%d sphere verts)", sphere_vert_count_);
}

int PointLightSystem::AddLight(const PointLight& light) {
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (!active_[i]) {
            lights_[i] = light;
            active_[i] = true;
            ++light_count_;
            return i;
        }
    }
    MD_LOG(MD_LOG_WARNING, "PointLightSystem: MAX_POINT_LIGHTS reached");
    return -1;
}

void PointLightSystem::RemoveLight(int idx) {
    if (idx < 0 || idx >= MAX_POINT_LIGHTS || !active_[idx]) return;
    active_[idx] = false;
    --light_count_;
}

void PointLightSystem::UpdateLight(int idx, const PointLight& light) {
    if (idx >= 0 && idx < MAX_POINT_LIGHTS && active_[idx])
        lights_[idx] = light;
}

void PointLightSystem::Clear() {
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) active_[i] = false;
    light_count_ = 0;
}

void PointLightSystem::Draw(SDL_GPUCommandBuffer* cmd,
                             SDL_GPUTexture*       gbuf_rt0,
                             SDL_GPUTexture*       gbuf_rt1,
                             SDL_GPUTexture*       gbuf_depth,
                             SDL_GPUSampler*       gbuf_sampler,
                             SDL_GPUTexture*       swapchain_tex,
                             const float*          view_proj_16,
                             const float*          inv_view_proj_16,
                             const float*          cam_pos_3,
                             int screen_w, int screen_h) {
    if (!enabled_ || light_count_ == 0) return;

    // Open additive pass on swapchain (LOAD existing content)
    SDL_GPUColorTargetInfo ct = {};
    ct.texture    = swapchain_tex;
    ct.load_op    = SDL_GPU_LOADOP_LOAD;
    ct.store_op   = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "PointLightSystem: SDL_BeginGPURenderPass failed");
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline_.SDLPipeline());

    // Bind sphere vertex buffer
    SDL_GPUBufferBinding vb = {};
    vb.buffer = sphere_verts_.SDLBuffer();
    vb.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

    // GBuffer samplers bound per-draw (same for all lights)
    SDL_GPUTextureSamplerBinding gbuf_bindings[3] = {};
    gbuf_bindings[0].texture = gbuf_rt0;   gbuf_bindings[0].sampler = gbuf_sampler;
    gbuf_bindings[1].texture = gbuf_rt1;   gbuf_bindings[1].sampler = gbuf_sampler;
    gbuf_bindings[2].texture = gbuf_depth; gbuf_bindings[2].sampler = gbuf_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, gbuf_bindings, 3);

    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (!active_[i]) continue;
        const PointLight& L = lights_[i];

        // Vertex UBO: MVP = VP × scale_translate(pos, radius)
        float model[16], mvp[16];
        SphereMat4(model, L.pos, L.radius);
        Mat4Mul(mvp, view_proj_16, model);

        PointLightVertUBO vubo;
        memcpy(vubo.mvp, mvp, 64);
        SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

        // Fragment UBO
        PointLightFragUBO fubo;
        fubo.light_pos[0] = L.pos[0]; fubo.light_pos[1] = L.pos[1]; fubo.light_pos[2] = L.pos[2];
        fubo.radius       = L.radius;
        fubo.light_col[0] = L.color[0]; fubo.light_col[1] = L.color[1]; fubo.light_col[2] = L.color[2];
        fubo.intensity    = L.intensity;
        fubo.cam_pos[0]   = cam_pos_3[0]; fubo.cam_pos[1] = cam_pos_3[1]; fubo.cam_pos[2] = cam_pos_3[2];
        fubo._pad0        = 0.f;
        fubo.scr_w        = (float)screen_w;
        fubo.scr_h        = (float)screen_h;
        fubo._pad1[0]     = 0.f; fubo._pad1[1] = 0.f;
        memcpy(fubo.inv_vp, inv_view_proj_16, 64);
        SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

        SDL_DrawGPUPrimitives(pass, (uint32_t)sphere_vert_count_, 1, 0, 0);
    }

    SDL_EndGPURenderPass(pass);
}

void PointLightSystem::Shutdown() {
    if (!dev_) return;
    pipeline_.Destroy();
    sphere_verts_.Shutdown();
    dev_     = nullptr;
    enabled_ = false;
    light_count_ = 0;
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) active_[i] = false;
}

} // namespace md
#endif // MD_SDL_GPU
