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

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#endif

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
    // Create SDL_GPU depth textures for the shadow cascades.
    // In dual-backend mode, Init() creates them; call this only in pure SDL_GPU mode.
    void InitSDLGPU() {
        for (int k = 0; k < NUM_CASCADES; ++k)
            if (!shadow_depth_[k].SDLTexture())
                shadow_depth_[k].Init(MAP_SIZE, MAP_SIZE, /*shadow_border=*/true);
    }
#endif

    void Init(MdMesh npc_mesh) {
#ifdef MD_OPENGL43_ENABLED
        if (init_) return;
        mesh_     = npc_mesh;
        mesh_idx_ = npc_mesh.index_count;

        // 3 cascade depth textures + FBOs — managed by GpuDepthTexture.
        // shadow_border=true: GL_CLAMP_TO_BORDER + white border for PCF off-map reads.
        for (int k = 0; k < NUM_CASCADES; ++k)
            shadow_depth_[k].Init(MAP_SIZE, MAP_SIZE, /*shadow_border=*/true);

        shadow_shader_ = MdLoadShader("shaders/shadow_csm.vert",
                                      "shaders/shadow_csm.frag");
        loc_lightVP_   = MdGetLoc(shadow_shader_, "lightViewProj");

        // Shadow cull compute — GpuComputePipeline replaces raw ComputeShader.
        // SDL_GPU SPIRV: set=0 ro[0]=Transform; set=1 rw[0]=ShadowVis, rw[1]=ShadowInd; set=2 UBO.
        GpuComputePipeline::Desc sc_desc;
        sc_desc.glsl_path                    = "shaders/shadow_cull.comp";
        sc_desc.num_uniform_buffers          = 1; // camPos/maxDist/count (set=2 binding=0)
        sc_desc.num_readonly_storage_buffers = 1; // TransformBuf (set=0 binding=0)
        sc_desc.num_readwrite_storage_buffers= 2; // ShadowVisBuf(set=1,bind=0), ShadowIndBuf(bind=1)
        shadow_cull_cs_.Create(sc_desc);
        loc_svCamPos_  = shadow_cull_cs_.UniformLoc("camPos");
        loc_svMaxDist_ = shadow_cull_cs_.UniformLoc("maxDistSq");
        loc_svTotal_   = shadow_cull_cs_.UniformLoc("total_count");

        // MAX_SLOTS = 8192, indirect = 5 uint32 = 20 bytes
        shadow_vis_buf_.Init(8192 * (int)sizeof(uint32_t));
        shadow_ind_buf_.Init(20);
        init_ = true;
#endif
    }

    // Recompute light-space VP matrices from camera frustum corners.
    void Update(const MdCamera& camera, Vec3 sun_dir, int sw, int sh) {
#ifdef MD_OPENGL43_ENABLED
        if (!init_) return;
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

            Mat4 lp = mat4_ortho(minX, maxX, minY, maxY, minZ, maxZ);
            lightViewProj[k] = mat4_mul(lv, lp);
        }
#endif
    }

    // Shadow cull + depth render.
    // Caller must pre-bind: transform SSBO at 0, finalBones SSBO at 4.
    // SDL_GPU: pass bindings.cmd; rw[0]=shadow_vis, rw[1]=shadow_ind, ro[0]=transform.
    //          bindings.cmd=nullptr is a safe no-op (Step 9 wires SDL_GPUBuffer*).
    void RenderShadowPass(int active_npc_count,
                          const GpuComputePass::StorageBindings& bindings = {}) {
#ifdef MD_OPENGL43_ENABLED
        if (!init_ || active_npc_count <= 0) return;

        // Reset indirect command (instanceCount = 0).
        uint32_t cmd[5] = { (uint32_t)mesh_idx_, 0u, 0u, 0u, 0u };
        shadow_ind_buf_.Upload(cmd, 20, 0);
        shadow_vis_buf_.Bind(6);
        shadow_ind_buf_.Bind(7);

        // Shadow cull compute — GpuComputePass replaces raw ComputeShader dispatch.
#ifdef MD_SDL_GPU
        if (bindings.cmd) {
            // SDL_GPU path: push camPos/maxDistSq/totalCount as UBO slot 0.
            struct alignas(16) ShadowCullUBO {
                float camPos[3]; float _p;
                float maxDistSq; float _p2[3];
                int   totalCount; int _p3[3];
            } ubo = { { cam_pos_.x, cam_pos_.y, cam_pos_.z }, 0.f,
                       60.f * 60.f, {}, active_npc_count, {} };
            GpuComputePass cull;
            cull.Begin(&shadow_cull_cs_, bindings);
            cull.PushUniforms(0, &ubo, sizeof(ubo));
            cull.Dispatch(((unsigned)active_npc_count + 63u) / 64u, 1u, 1u);
            cull.End();
        }
#endif
        {
            float cpf[3] = { cam_pos_.x, cam_pos_.y, cam_pos_.z };
            GpuComputePass cull;
            cull.Begin(&shadow_cull_cs_);
            cull.SetUniformVec3  (loc_svCamPos_,  cpf);
            cull.SetUniformFloat (loc_svMaxDist_, 60.f * 60.f);
            cull.SetUniformInt   (loc_svTotal_,   active_npc_count);
            cull.Dispatch(((unsigned)active_npc_count + 63u) / 64u, 1u, 1u);
            // STORAGE | COMMAND: indirect buffer written by compute, read by draw call.
            cull.End(GpuComputePass::BARRIER_STORAGE_COMMAND);
        }

        // Depth pass per cascade — GpuRenderPass manages FBO bind/viewport/clear.
        MdUseShader(shadow_shader_);
        shadow_vis_buf_.Bind(6); // re-bind after compute (may have unbound SSBOs)
        glBindVertexArray(mesh_.vao);

        for (int k = 0; k < NUM_CASCADES; ++k) {
            GpuRenderPass pass;
            pass.BeginDepthOnly({ &shadow_depth_[k], 1.0f, /*cull_front=*/true });
            glUniformMatrix4fv(loc_lightVP_, 1, GL_FALSE, mat4_ptr(lightViewProj[k]));
            GpuDrawIndexedIndirect(shadow_ind_buf_.id);
            pass.End();
        }

        glBindVertexArray(0);
        MdStopShader();
#endif
    }

    // Binds shadow maps to texture units 5/6/7 and sets CSM uniforms.
    // Caller must have the npc shader active (glUseProgram / MdUseShader).
    void BindShadowMaps(MdShader shader) {
#ifdef MD_OPENGL43_ENABLED
        if (!init_) return;

        // GpuDepthTexture::Bind handles glActiveTexture + glBindTexture.
        for (int k = 0; k < NUM_CASCADES; ++k)
            shadow_depth_[k].Bind((uint32_t)(5 + k));
        glActiveTexture(GL_TEXTURE0);

        char nm[32];
        for (int k = 0; k < NUM_CASCADES; ++k) {
            snprintf(nm, sizeof(nm), "shadowMaps[%d]", k);
            int loc = MdGetLoc(shader, nm);
            if (loc >= 0) glUniform1i(loc, 5 + k);
        }

        for (int k = 0; k < NUM_CASCADES; ++k) {
            snprintf(nm, sizeof(nm), "lightViewProj[%d]", k);
            int loc = MdGetLoc(shader, nm);
            if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, mat4_ptr(lightViewProj[k]));
        }

        int loc_s = MdGetLoc(shader, "cascadeSplits");
        if (loc_s >= 0) glUniform1fv(loc_s, 3, cascade_splits);
#endif
    }

    void Shutdown() {
#ifdef MD_OPENGL43_ENABLED
        if (!init_) return;
        for (int k = 0; k < NUM_CASCADES; ++k)
            shadow_depth_[k].Shutdown();
        MdUnloadShader(shadow_shader_);
        shadow_cull_cs_.Destroy();
        shadow_vis_buf_.Shutdown();
        shadow_ind_buf_.Shutdown();
        init_ = false;
#endif
    }

private:
    ShadowSystem() = default;

    bool               init_     = false;
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
