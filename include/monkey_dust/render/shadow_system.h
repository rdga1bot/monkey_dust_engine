#pragma once
#include <monkey_dust/platform/math_types.h>
#include <monkey_dust/render/ssbo.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/md_camera.h>
#include <monkey_dust/render/md_shader.h>
#include <monkey_dust/render/md_mesh.h>
#include <cmath>
#include <cstdio>
#include <cstdint>


#ifndef DEG2RAD
#define DEG2RAD 0.01745329251f
#endif

// Vector4Transform was removed from raymath in Raylib 6.x.
// In GLM mode, matrix * vector handles this directly.
#ifdef USE_GLM
static inline Vec4 ShadowVec4Transform(Vec4 v, Mat4 m) { return m * v; }
#else
static inline Vec4 ShadowVec4Transform(Vec4 v, Mat4 m) {
    return {
        m.m0*v.x + m.m4*v.y + m.m8 *v.z + m.m12*v.w,
        m.m1*v.x + m.m5*v.y + m.m9 *v.z + m.m13*v.w,
        m.m2*v.x + m.m6*v.y + m.m10*v.z + m.m14*v.w,
        m.m3*v.x + m.m7*v.y + m.m11*v.z + m.m15*v.w,
    };
}
#endif


// ─────────────────────────────────────────────────────────
// ShadowSystem — 3-cascade CSM for the directional sun.
//
// Cascades (dist from camera): near 0-20 m, mid 20-60 m, far 60-150 m.
// Per cascade: 1024×1024 GL_DEPTH_COMPONENT24 depth texture.
//
// Per-frame flow (caller side):
//   1. Update(camera, sun_dir)               — recompute lightViewProj[3]
//   2. transform_ssbo.Bind(0); bones.Bind(4) — caller pre-binds SSBOs
//   3. RenderShadowPass(active_count)        — shadow cull + depth pass
//   4. (inside DrawNPCs, shader enabled):
//      BindShadowMaps(shader)                — tex units 5/6/7 + uniforms
//
// SSBOs used internally: binding 6 = shadow visible, 7 = shadow indirect
// Texture units assigned: 5/6/7 = cascade 0/1/2
// ─────────────────────────────────────────────────────────

class ShadowSystem {
public:
    static constexpr int   NUM_CASCADES = 3;
    static constexpr int   MAP_SIZE     = 1024;
    static constexpr float SPLITS[4]    = { 0.1f, 20.0f, 60.0f, 150.0f };

    static ShadowSystem& Get() {
        static ShadowSystem inst;
        return inst;
    }

    Mat4  lightViewProj[NUM_CASCADES]  = {};
    float cascade_splits[NUM_CASCADES] = { SPLITS[1], SPLITS[2], SPLITS[3] };

    // Read-only access to cascade depth textures (SDL_GPU sampler binding, Step 11).
    const GpuDepthTexture& GetCascadeDepth(int k) const { return shadow_depth_[k]; }

#ifdef MD_SDL_GPU
    // Creates depth textures + shadow cull compute + graphics pipeline.
    // Called from InitNpcSDL() in pure SDL_GPU builds.
    void InitSDLGPU(uint32_t npc_idx_count) {
        for (int k = 0; k < NUM_CASCADES; ++k)
            if (!shadow_depth_[k].SDLTexture())
                shadow_depth_[k].Init(MAP_SIZE, MAP_SIZE, /*shadow_border=*/true);
        if (sdl_init_) return;
        npc_idx_count_ = npc_idx_count;

        GpuComputePipeline::Desc sc;
        sc.glsl_path                    = "shaders/shadow_cull.comp";
        sc.num_uniform_buffers          = 1;
        sc.num_readonly_storage_buffers = 1;
        sc.num_readwrite_storage_buffers= 2;
        shadow_cull_cs_.Create(sc);

        shadow_vis_buf_.Init(8192 * (int)sizeof(uint32_t));
        shadow_ind_buf_.Init(20, SSBO_INDIRECT);

        // shadow_csm.vert: a_pos(loc=0,F3,stride=24 pos+norm) +
        //   storage 0=Transform 1=ShadowVis 2=FinalBones; UBO 0=lightViewProj.
        // shadow_csm.frag: no outputs, no uniforms.
        GpuPipeline::Desc pd;
        pd.vert_path         = "shaders/shadow_csm.vert";
        pd.frag_path         = "shaders/shadow_csm.frag";
        pd.vert_uniform_bufs = 1;
        pd.vert_storage_bufs = 3;
        pd.depth_only        = true;
        pd.raster.cull_back  = false; // cull_front for Peter-Panning; not pipeline-configurable in SDL_GPU
        pd.layout.attribs[0] = { 0, 0, GpuAttribFmt::F3 };
        pd.layout.count      = 1;
        pd.layout.stride     = 24; // pos+norm, same sphere as NPC
        shadow_pipeline_.Create(pd);

        sdl_init_ = true;
    }

    void RenderShadowPassSDL(SDL_GPUCommandBuffer* cmd, int active_npc_count,
                              SDL_GPUBuffer* transform_buf, SDL_GPUBuffer* bones_buf,
                              SDL_GPUBuffer* npc_vbuf, SDL_GPUBuffer* npc_ibuf,
                              bool npc_idx_u16 = true) {
        if (!sdl_init_ || active_npc_count <= 0 || !cmd) return;
        if (!shadow_pipeline_.SDLPipeline()) return;

        // Reset shadow indirect command (indexCount = npc_idx_count_, instanceCount = 0).
        uint32_t reset[5] = { npc_idx_count_, 0u, 0u, 0u, 0u };
        shadow_ind_buf_.UploadInCmd(cmd, reset, 20);

        // Shadow cull compute — select NPCs within 60 m of camera.
        struct alignas(16) SCullUBO {
            float camPos[3]; float _p;
            float maxDistSq; float _p2[3];
            int   totalCount; int _p3[3];
        } ubo = { {cam_pos_.x, cam_pos_.y, cam_pos_.z}, 0.f,
                   60.f*60.f, {}, active_npc_count, {} };
        GpuComputePass::StorageBindings b;
        b.cmd = cmd;
        b.rw_buffers[0] = { shadow_vis_buf_.SDLBuffer(), false };
        b.rw_buffers[1] = { shadow_ind_buf_.SDLBuffer(), false };
        b.num_rw_buffers = 2;
        b.ro_buffers[0]  = transform_buf;
        b.num_ro_buffers = 1;
        GpuComputePass cull;
        cull.Begin(&shadow_cull_cs_, b);
        cull.PushUniforms(0, &ubo, sizeof(ubo));
        cull.Dispatch(((uint32_t)active_npc_count + 63u) / 64u, 1u, 1u);
        cull.End();

        // 3 depth-only cascade passes with draw calls.
        for (int k = 0; k < NUM_CASCADES; ++k) {
            GpuRenderPass pass;
            pass.BeginDepthOnly(cmd, { &shadow_depth_[k], 1.0f, /*cull_front=*/true });
            SDL_GPURenderPass* rp = pass.SDLPass();
            if (rp) {
                SDL_BindGPUGraphicsPipeline(rp, shadow_pipeline_.SDLPipeline());
                SDL_GPUBufferBinding vb = { npc_vbuf, 0 };
                SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
                SDL_GPUBufferBinding ib = { npc_ibuf, 0 };
                SDL_BindGPUIndexBuffer(rp, &ib,
                    npc_idx_u16 ? SDL_GPU_INDEXELEMENTSIZE_16BIT
                                : SDL_GPU_INDEXELEMENTSIZE_32BIT);
                SDL_GPUBuffer* sv[3] = {
                    transform_buf,
                    shadow_vis_buf_.SDLBuffer(),
                    bones_buf
                };
                SDL_BindGPUVertexStorageBuffers(rp, 0, sv, 3);
                SDL_PushGPUVertexUniformData(cmd, 0, mat4_ptr(lightViewProj[k]), 64);
                SDL_DrawGPUIndexedPrimitivesIndirect(rp, shadow_ind_buf_.SDLBuffer(), 0, 1);
            }
            pass.End();
        }
    }
#endif

    void Init(MdMesh npc_mesh) {
    }

    // Recompute light-space VP matrices from camera frustum corners.
    // Pure CPU math — no GL calls; works in both OpenGL and SDL_GPU paths.
    void Update(const MdCamera& camera, Vec3 sun_dir, int sw, int sh) {
        if (!sdl_init_) return;
        cam_pos_  = camera.pos;
        screen_w_ = (sw > 0) ? sw : 1280;
        screen_h_ = (sh > 0) ? sh : 720;

        float aspect = (float)screen_w_ / (float)screen_h_;
        float fovY   = camera.fovy * DEG2RAD;

        Vec3 fwd   = vec3_norm(vec3_sub(camera.target, camera.pos));
        Vec3 right = vec3_norm(vec3_cross(fwd, Vec3{ 0.f, 1.f, 0.f }));
        Vec3 up_c  = vec3_cross(right, fwd);

        Vec3 to_sun = { -sun_dir.x, -sun_dir.y, -sun_dir.z };
        Vec3 lup    = (fabsf(sun_dir.y) < 0.99f) ?
                       Vec3{ 0.f, 1.f, 0.f } : Vec3{ 1.f, 0.f, 0.f };

        Vec3 cam_p = camera.pos;
        for (int k = 0; k < NUM_CASCADES; ++k) {
            float nk = SPLITS[k],  fk = SPLITS[k + 1];
            float tanH = tanf(fovY * 0.5f);
            float nH = nk * tanH, nW = nH * aspect;
            float fH = fk * tanH, fW = fH * aspect;

            Vec3 nc = vec3_add(cam_p, vec3_scale(fwd, nk));
            Vec3 fc = vec3_add(cam_p, vec3_scale(fwd, fk));

            Vec3 corners[8] = {
                vec3_add(vec3_add(nc, vec3_scale(up_c,  nH)), vec3_scale(right,  nW)),
                vec3_add(vec3_add(nc, vec3_scale(up_c,  nH)), vec3_scale(right, -nW)),
                vec3_add(vec3_add(nc, vec3_scale(up_c, -nH)), vec3_scale(right,  nW)),
                vec3_add(vec3_add(nc, vec3_scale(up_c, -nH)), vec3_scale(right, -nW)),
                vec3_add(vec3_add(fc, vec3_scale(up_c,  fH)), vec3_scale(right,  fW)),
                vec3_add(vec3_add(fc, vec3_scale(up_c,  fH)), vec3_scale(right, -fW)),
                vec3_add(vec3_add(fc, vec3_scale(up_c, -fH)), vec3_scale(right,  fW)),
                vec3_add(vec3_add(fc, vec3_scale(up_c, -fH)), vec3_scale(right, -fW)),
            };

            Vec3 center = {};
            for (int i = 0; i < 8; ++i)
                center = vec3_add(center, vec3_scale(corners[i], 0.125f));

            Mat4 lv = mat4_lookat(vec3_add(center, to_sun), center, lup);

            float minX = 1e9f, maxX = -1e9f;
            float minY = 1e9f, maxY = -1e9f;
            float minZ = 1e9f, maxZ = -1e9f;
            for (int i = 0; i < 8; ++i) {
                Vec4 lc = ShadowVec4Transform(
                    { corners[i].x, corners[i].y, corners[i].z, 1.f }, lv);
                if (lc.x < minX) minX = lc.x; if (lc.x > maxX) maxX = lc.x;
                if (lc.y < minY) minY = lc.y; if (lc.y > maxY) maxY = lc.y;
                if (lc.z < minZ) minZ = lc.z; if (lc.z > maxZ) maxZ = lc.z;
            }
            minZ -= 50.0f; // extend backward to catch casters behind the slice

            // Texel-snap: quantize the light-space frustum center to the nearest
            // shadow-map texel so the shadow map moves in whole-texel steps.
            // Eliminates shadow shimmer (sub-texel jitter) during camera movement.
            {
                float texelX = (maxX - minX) / MAP_SIZE;
                float texelY = (maxY - minY) / MAP_SIZE;
                float cx = (minX + maxX) * 0.5f;
                float cy = (minY + maxY) * 0.5f;
                cx = roundf(cx / texelX) * texelX;
                cy = roundf(cy / texelY) * texelY;
                float rx = (maxX - minX) * 0.5f;
                float ry = (maxY - minY) * 0.5f;
                minX = cx - rx;  maxX = cx + rx;
                minY = cy - ry;  maxY = cy + ry;
            }

            Mat4 lp = mat4_ortho(minX, maxX, minY, maxY, minZ, maxZ);
#ifdef MD_SDL_GPU
            lightViewProj[k] = mat4_mul(lp, lv);
#else
            lightViewProj[k] = mat4_mul(lv, lp);
#endif
        }
    }

    // Shadow cull + depth render.
    // Caller must pre-bind: transform SSBO at 0, finalBones SSBO at 4.
    // SDL_GPU: pass bindings.cmd; rw[0]=shadow_vis, rw[1]=shadow_ind, ro[0]=transform.
    //          bindings.cmd=nullptr is a safe no-op (Step 9 wires SDL_GPUBuffer*).
    void RenderShadowPass(int active_npc_count,
                          const GpuComputePass::StorageBindings& bindings = {}) {
    }

    // Binds shadow maps to texture units 5/6/7 and sets CSM uniforms.
    // Caller must have the npc shader active (glUseProgram / MdUseShader).
    void BindShadowMaps(MdShader shader) {
    }

    void Shutdown() {
    }

private:
    ShadowSystem() = default;

    bool               init_         = false;
#ifdef MD_SDL_GPU
    bool               sdl_init_     = false;
    GpuPipeline        shadow_pipeline_;        // shadow_csm.vert/frag (depth_only)
    uint32_t           npc_idx_count_ = 0;
#endif
    GpuDepthTexture    shadow_depth_[NUM_CASCADES]; // tex + FBO per cascade
    MdShader           shadow_shader_;
    GpuComputePipeline shadow_cull_cs_;             // shadow_cull.comp
    SSBO               shadow_vis_buf_;
    SSBO               shadow_ind_buf_;
    MdMesh        mesh_;
    int           mesh_idx_ = 0;
    Vec3          cam_pos_ = {};
    int  loc_lightVP_  = -1;
    int  loc_svCamPos_ = -1, loc_svMaxDist_ = -1, loc_svTotal_ = -1;
    int  screen_w_ = 1280, screen_h_ = 720;
};
