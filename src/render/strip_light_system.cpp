#ifdef MD_SDL_GPU
#include <monkey_dust/render/strip_light_system.h>
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/platform/md_log.h>
#include <cmath>
#include <cstring>

// ── UBO layouts (std140, must match GLSL) ────────────────────────────────────

struct StripVertUBO {
    float mvp[16];       // mat4 64B
};
static_assert(sizeof(StripVertUBO) == 64);

struct StripFragUBO {
    float p0[3];       float radius;    // vec4 @  0
    float p1[3];       float _pad0;     // vec4 @ 16
    float col[3];      float intensity; // vec4 @ 32
    float cam_pos[3];  float _pad1;     // vec4 @ 48
    float scr_w, scr_h; float _pad2[2]; // vec4 @ 64
    float inv_vp[16];                    // mat4 @ 80
};
static_assert(sizeof(StripFragUBO) == 144);

// ── Unit AABB (36 vertices = 12 tris, non-indexed) ───────────────────────────
// Vertices in [-1,1]^3 — model matrix will scale to capsule AABB.

static void GenUnitBox(float* out, int& count) {
    static const float faces[6][4][3] = {
        // +X
        {{1,-1,-1},{1, 1,-1},{1, 1, 1},{1,-1, 1}},
        // -X
        {{-1,-1, 1},{-1, 1, 1},{-1, 1,-1},{-1,-1,-1}},
        // +Y
        {{-1, 1,-1},{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1}},
        // -Y
        {{-1,-1, 1},{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1}},
        // +Z
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        // -Z
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}}
    };
    // Triangle order: 0,1,2  0,2,3 per face
    count = 0;
    for (int f = 0; f < 6; ++f) {
        const int idx[6] = {0,1,2, 0,2,3};
        for (int k = 0; k < 6; ++k) {
            const float* v = faces[f][idx[k]];
            out[count*3+0] = v[0];
            out[count*3+1] = v[1];
            out[count*3+2] = v[2];
            ++count;
        }
    }
    // count == 36
}

// ── Capsule AABB model matrix ────────────────────────────────────────────────
// Scale the unit box to the AABB that tightly bounds the capsule.
// AABB = midpoint ± (half-len + radius) along each axis.

static void CapsuleMat4(float M[16], const float p0[3], const float p1[3], float r) {
    float cx = (p0[0]+p1[0])*0.5f;
    float cy = (p0[1]+p1[1])*0.5f;
    float cz = (p0[2]+p1[2])*0.5f;
    float ex = fabsf(p1[0]-p0[0])*0.5f + r;
    float ey = fabsf(p1[1]-p0[1])*0.5f + r;
    float ez = fabsf(p1[2]-p0[2])*0.5f + r;

    memset(M, 0, 64);
    M[0]  = ex; M[5]  = ey; M[10] = ez;
    M[12] = cx; M[13] = cy; M[14] = cz;
    M[15] = 1.f;
}

static void Mat4Mul(float R[16], const float A[16], const float B[16]) {
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k)
                s += A[k*4+row] * B[col*4+k];
            R[col*4+row] = s;
        }
}

// ── StripLightSystem ─────────────────────────────────────────────────────────

namespace md {

StripLightSystem& StripLightSystem::Get() {
    static StripLightSystem inst;
    return inst;
}

void StripLightSystem::Init(SDL_GPUDevice* dev) {
    if (!RenderTierSystem::Get().IsDeferred()) {
        MD_LOG(MD_LOG_INFO, "StripLightSystem: Forward tier — disabled");
        return;
    }
    dev_ = dev;

    static float box_data[36 * 3];
    int box_count = 0;
    GenUnitBox(box_data, box_count);
    box_verts_.Init(0x8892 /*GL_ARRAY_BUFFER*/, box_data,
                    (uint32_t)(box_count * 3 * sizeof(float)));

    GpuPipeline::Desc d;
    d.vert_path = "shaders/deferred_strip.vert";
    d.frag_path = "shaders/deferred_strip.frag";
    d.layout.count      = 1;
    d.layout.attribs[0] = { 0, 0, GpuAttribFmt::F3 };
    d.layout.stride     = sizeof(float) * 3;
    d.raster.blend_enable = true;
    d.raster.src_factor   = GpuBlendFactor::ONE;
    d.raster.dst_factor   = GpuBlendFactor::ONE;
    d.raster.depth_test   = false;
    d.raster.depth_write  = false;
    d.raster.cull_back    = false;
    d.vert_uniform_bufs   = 1;
    d.frag_uniform_bufs   = 1;
    d.frag_samplers       = 3;
    d.has_depth_target    = false;
    d.depth_only          = false;

    if (!pipeline_.Create(d)) {
        MD_LOG(MD_LOG_WARNING, "StripLightSystem: pipeline create failed");
        return;
    }

    enabled_ = true;
    MD_LOG(MD_LOG_INFO, "StripLightSystem: ready (MAX=%d capsule lights)", MAX_STRIP_LIGHTS);
}

int StripLightSystem::AddLight(const StripLight& light) {
    for (int i = 0; i < MAX_STRIP_LIGHTS; ++i) {
        if (!active_[i]) {
            lights_[i] = light;
            active_[i] = true;
            ++light_count_;
            return i;
        }
    }
    MD_LOG(MD_LOG_WARNING, "StripLightSystem: MAX_STRIP_LIGHTS reached");
    return -1;
}

void StripLightSystem::RemoveLight(int idx) {
    if (idx >= 0 && idx < MAX_STRIP_LIGHTS && active_[idx]) {
        active_[idx] = false;
        --light_count_;
    }
}

void StripLightSystem::UpdateLight(int idx, const StripLight& light) {
    if (idx >= 0 && idx < MAX_STRIP_LIGHTS && active_[idx])
        lights_[idx] = light;
}

void StripLightSystem::Clear() {
    for (int i = 0; i < MAX_STRIP_LIGHTS; ++i) active_[i] = false;
    light_count_ = 0;
}

void StripLightSystem::Draw(SDL_GPUCommandBuffer* cmd,
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

    SDL_GPUColorTargetInfo ct = {};
    ct.texture  = swapchain_tex;
    ct.load_op  = SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    if (!pass) {
        MD_LOG(MD_LOG_WARNING, "StripLightSystem: SDL_BeginGPURenderPass failed");
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, pipeline_.SDLPipeline());

    SDL_GPUBufferBinding vb = {};
    vb.buffer = box_verts_.SDLBuffer();
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

    SDL_GPUTextureSamplerBinding gbuf_bindings[3] = {
        { gbuf_rt0,    gbuf_sampler },
        { gbuf_rt1,    gbuf_sampler },
        { gbuf_depth,  gbuf_sampler }
    };
    SDL_BindGPUFragmentSamplers(pass, 0, gbuf_bindings, 3);

    for (int i = 0; i < MAX_STRIP_LIGHTS; ++i) {
        if (!active_[i]) continue;
        const StripLight& L = lights_[i];

        float model[16], mvp[16];
        CapsuleMat4(model, L.p0, L.p1, L.radius);
        Mat4Mul(mvp, view_proj_16, model);

        StripVertUBO vubo;
        memcpy(vubo.mvp, mvp, 64);
        SDL_PushGPUVertexUniformData(cmd, 0, &vubo, sizeof(vubo));

        StripFragUBO fubo;
        fubo.p0[0]=L.p0[0]; fubo.p0[1]=L.p0[1]; fubo.p0[2]=L.p0[2];
        fubo.radius = L.radius;
        fubo.p1[0]=L.p1[0]; fubo.p1[1]=L.p1[1]; fubo.p1[2]=L.p1[2];
        fubo._pad0 = 0.f;
        fubo.col[0]=L.color[0]; fubo.col[1]=L.color[1]; fubo.col[2]=L.color[2];
        fubo.intensity = L.intensity;
        fubo.cam_pos[0]=cam_pos_3[0]; fubo.cam_pos[1]=cam_pos_3[1]; fubo.cam_pos[2]=cam_pos_3[2];
        fubo._pad1 = 0.f;
        fubo.scr_w = (float)screen_w;
        fubo.scr_h = (float)screen_h;
        fubo._pad2[0] = 0.f; fubo._pad2[1] = 0.f;
        memcpy(fubo.inv_vp, inv_view_proj_16, 64);
        SDL_PushGPUFragmentUniformData(cmd, 0, &fubo, sizeof(fubo));

        SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);
    }

    SDL_EndGPURenderPass(pass);
}

void StripLightSystem::Shutdown() {
    if (!dev_) return;
    pipeline_.Destroy();
    box_verts_.Shutdown();
    dev_     = nullptr;
    enabled_ = false;
    light_count_ = 0;
    for (int i = 0; i < MAX_STRIP_LIGHTS; ++i) active_[i] = false;
}

} // namespace md
#endif // MD_SDL_GPU
