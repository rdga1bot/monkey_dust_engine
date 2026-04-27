#pragma once
#include "raylib.h"
#include "raymath.h"
#include <monkey_dust/render/ssbo.h>
#include <monkey_dust/render/compute_shader.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#ifdef USE_SDL3
#  include "render/MdCamera.h"
#  include "render/MdShader.h"
#  include "render/MdMesh.h"
#  include "math_types.h"
#endif

#ifdef MD_OPENGL43_ENABLED
#include "rlgl.h"
#include "external/glad.h"
#endif

// Vector4Transform was removed from raymath in Raylib 6.x
static inline Vector4 ShadowVec4Transform(Vector4 v, Matrix m) {
    return {
        m.m0*v.x + m.m4*v.y + m.m8 *v.z + m.m12*v.w,
        m.m1*v.x + m.m5*v.y + m.m9 *v.z + m.m13*v.w,
        m.m2*v.x + m.m6*v.y + m.m10*v.z + m.m14*v.w,
        m.m3*v.x + m.m7*v.y + m.m11*v.z + m.m15*v.w,
    };
}

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

    Matrix lightViewProj[NUM_CASCADES]  = {};
    float  cascade_splits[NUM_CASCADES] = { SPLITS[1], SPLITS[2], SPLITS[3] };

#ifdef USE_SDL3
    void Init(MdMesh npc_mesh) {
#else
    void Init(Mesh npc_mesh) {
#endif
#ifdef MD_OPENGL43_ENABLED
        if (init_) return;
        mesh_     = npc_mesh;
#ifdef USE_SDL3
        mesh_idx_ = npc_mesh.index_count;
#else
        mesh_idx_ = npc_mesh.triangleCount * 3;
#endif

        for (int k = 0; k < NUM_CASCADES; ++k) {
            glGenTextures(1, &shadow_tex_[k]);
            glBindTexture(GL_TEXTURE_2D, shadow_tex_[k]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                         MAP_SIZE, MAP_SIZE, 0,
                         GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float border[4] = { 1.f, 1.f, 1.f, 1.f };
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
            glBindTexture(GL_TEXTURE_2D, 0);

            glGenFramebuffers(1, &fbo_[k]);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_[k]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_TEXTURE_2D, shadow_tex_[k], 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

#ifdef USE_SDL3
        shadow_shader_ = MdLoadShader("shaders/shadow_csm.vert",
                                      "shaders/shadow_csm.frag");
        loc_lightVP_   = MdGetLoc(shadow_shader_, "lightViewProj");
#else
        shadow_shader_ = LoadShader("shaders/shadow_csm.vert",
                                    "shaders/shadow_csm.frag");
        loc_lightVP_   = GetShaderLocation(shadow_shader_, "lightViewProj");
#endif

        shadow_cull_cs_.Load("shaders/shadow_cull.comp");
        loc_svCamPos_  = shadow_cull_cs_.GetUniformLoc("camPos");
        loc_svMaxDist_ = shadow_cull_cs_.GetUniformLoc("maxDistSq");
        loc_svTotal_   = shadow_cull_cs_.GetUniformLoc("total_count");

        // MAX_SLOTS = 8192, indirect = 5 uint32 = 20 bytes
        shadow_vis_buf_.Init(8192 * (int)sizeof(uint32_t));
        shadow_ind_buf_.Init(20);
        init_ = true;
#endif
    }

    // Recompute light-space VP matrices from camera frustum corners.
#ifdef USE_SDL3
    void Update(const MdCamera& camera, Vector3 sun_dir) {
        Camera3D rc = camera.ToRaylib();
        Update(rc, sun_dir);
    }
#endif
    void Update(const Camera3D& camera, Vector3 sun_dir) {
#ifdef MD_OPENGL43_ENABLED
        if (!init_) return;
        cam_pos_ = camera.position;

        float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();
        float fovY   = camera.fovy * DEG2RAD;

        Vector3 fwd   = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, { 0.f, 1.f, 0.f }));
        Vector3 up_c  = Vector3CrossProduct(right, fwd);

        Vector3 to_sun = { -sun_dir.x, -sun_dir.y, -sun_dir.z };
        Vector3 lup    = (fabsf(sun_dir.y) < 0.99f) ?
                          Vector3{ 0.f, 1.f, 0.f } : Vector3{ 1.f, 0.f, 0.f };

        for (int k = 0; k < NUM_CASCADES; ++k) {
            float nk = SPLITS[k],  fk = SPLITS[k + 1];
            float tanH = tanf(fovY * 0.5f);
            float nH = nk * tanH, nW = nH * aspect;
            float fH = fk * tanH, fW = fH * aspect;

            Vector3 nc = Vector3Add(camera.position, Vector3Scale(fwd, nk));
            Vector3 fc = Vector3Add(camera.position, Vector3Scale(fwd, fk));

            Vector3 corners[8] = {
                Vector3Add(Vector3Add(nc, Vector3Scale(up_c,  nH)), Vector3Scale(right,  nW)),
                Vector3Add(Vector3Add(nc, Vector3Scale(up_c,  nH)), Vector3Scale(right, -nW)),
                Vector3Add(Vector3Add(nc, Vector3Scale(up_c, -nH)), Vector3Scale(right,  nW)),
                Vector3Add(Vector3Add(nc, Vector3Scale(up_c, -nH)), Vector3Scale(right, -nW)),
                Vector3Add(Vector3Add(fc, Vector3Scale(up_c,  fH)), Vector3Scale(right,  fW)),
                Vector3Add(Vector3Add(fc, Vector3Scale(up_c,  fH)), Vector3Scale(right, -fW)),
                Vector3Add(Vector3Add(fc, Vector3Scale(up_c, -fH)), Vector3Scale(right,  fW)),
                Vector3Add(Vector3Add(fc, Vector3Scale(up_c, -fH)), Vector3Scale(right, -fW)),
            };

            Vector3 center = {};
            for (int i = 0; i < 8; ++i)
                center = Vector3Add(center, Vector3Scale(corners[i], 0.125f));

            Matrix lv = MatrixLookAt(Vector3Add(center, to_sun), center, lup);

            float minX = 1e9f, maxX = -1e9f;
            float minY = 1e9f, maxY = -1e9f;
            float minZ = 1e9f, maxZ = -1e9f;
            for (int i = 0; i < 8; ++i) {
                Vector4 lc = ShadowVec4Transform(
                    { corners[i].x, corners[i].y, corners[i].z, 1.f }, lv);
                if (lc.x < minX) minX = lc.x; if (lc.x > maxX) maxX = lc.x;
                if (lc.y < minY) minY = lc.y; if (lc.y > maxY) maxY = lc.y;
                if (lc.z < minZ) minZ = lc.z; if (lc.z > maxZ) maxZ = lc.z;
            }
            minZ -= 50.0f; // extend backward to catch casters behind the slice

            Matrix lp = MatrixOrtho(minX, maxX, minY, maxY, minZ, maxZ);
            lightViewProj[k] = MatrixMultiply(lv, lp);
        }
#endif
    }

    // Shadow cull + depth render.
    // Caller must pre-bind: transform SSBO at 0, finalBones SSBO at 4.
    void RenderShadowPass(int active_npc_count) {
#ifdef MD_OPENGL43_ENABLED
        if (!init_ || active_npc_count <= 0) return;

        // Reset indirect command (instanceCount = 0)
        uint32_t cmd[5] = { (uint32_t)mesh_idx_, 0u, 0u, 0u, 0u };
        shadow_ind_buf_.Upload(cmd, 20, 0);
        shadow_vis_buf_.Bind(6);
        shadow_ind_buf_.Bind(7);

        // Shadow cull: distance < 60 m → LOD_HIGH + LOD_MID
        shadow_cull_cs_.Enable();
        float cpf[3]   = { cam_pos_.x, cam_pos_.y, cam_pos_.z };
        float maxDSq   = 60.f * 60.f;
        int   total    = active_npc_count;
#ifdef USE_SDL3
        glUniform3fv(loc_svCamPos_,  1, cpf);
        glUniform1f (loc_svMaxDist_, maxDSq);
        glUniform1i (loc_svTotal_,   total);
#else
        rlSetUniform(loc_svCamPos_,  cpf,    SHADER_UNIFORM_VEC3,  1);
        rlSetUniform(loc_svMaxDist_, &maxDSq, SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(loc_svTotal_,   &total,  SHADER_UNIFORM_INT,   1);
#endif
        shadow_cull_cs_.Dispatch(((unsigned)active_npc_count + 63u) / 64u, 1u, 1u);
        shadow_cull_cs_.Disable();
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);

        // Depth pass per cascade
#ifdef USE_SDL3
        glUseProgram(shadow_shader_.id);
#else
        rlEnableShader(shadow_shader_.id);
#endif
        shadow_vis_buf_.Bind(6); // re-bind after cull compute
        glCullFace(GL_FRONT);

        for (int k = 0; k < NUM_CASCADES; ++k) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_[k]);
            glViewport(0, 0, MAP_SIZE, MAP_SIZE);
            glClear(GL_DEPTH_BUFFER_BIT);

#ifdef USE_SDL3
            glUniformMatrix4fv(loc_lightVP_, 1, GL_FALSE, &lightViewProj[k].m0);
            glBindVertexArray(mesh_.vao);
#else
            rlSetUniformMatrix(loc_lightVP_, lightViewProj[k]);
            rlEnableVertexArray(mesh_.vaoId);
#endif
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, shadow_ind_buf_.id);
            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, 0, 1, 0);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
#ifdef USE_SDL3
            glBindVertexArray(0);
#else
            rlDisableVertexArray();
#endif
        }

        glCullFace(GL_BACK);
#ifdef USE_SDL3
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, GetScreenWidth(), GetScreenHeight());
#else
        rlDisableShader();
        rlDisableFramebuffer(); // restore main FBO + viewport
#endif
#endif
    }

    // Binds shadow maps to texture units 5/6/7 and sets CSM uniforms.
    // Caller must have the npc shader active (MdUseShader / rlEnableShader).
#ifdef USE_SDL3
    void BindShadowMaps(MdShader shader) {
#else
    void BindShadowMaps(Shader shader) {
#endif
#ifdef MD_OPENGL43_ENABLED
        if (!init_) return;

        for (int k = 0; k < NUM_CASCADES; ++k) {
            glActiveTexture(GL_TEXTURE5 + k);
            glBindTexture(GL_TEXTURE_2D, shadow_tex_[k]);
        }
        glActiveTexture(GL_TEXTURE0);

        char nm[32];
        for (int k = 0; k < NUM_CASCADES; ++k) {
            snprintf(nm, sizeof(nm), "shadowMaps[%d]", k);
#ifdef USE_SDL3
            int loc = MdGetLoc(shader, nm);
#else
            int loc = GetShaderLocation(shader, nm);
#endif
            if (loc >= 0) glUniform1i(loc, 5 + k);
        }

        for (int k = 0; k < NUM_CASCADES; ++k) {
            snprintf(nm, sizeof(nm), "lightViewProj[%d]", k);
#ifdef USE_SDL3
            int loc = MdGetLoc(shader, nm);
            if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, &lightViewProj[k].m0);
#else
            int loc = GetShaderLocation(shader, nm);
            if (loc >= 0) rlSetUniformMatrix(loc, lightViewProj[k]);
#endif
        }

#ifdef USE_SDL3
        int loc_s = MdGetLoc(shader, "cascadeSplits");
        if (loc_s >= 0) glUniform1fv(loc_s, 3, cascade_splits);
#else
        int loc_s = GetShaderLocation(shader, "cascadeSplits");
        if (loc_s >= 0) rlSetUniform(loc_s, cascade_splits, SHADER_UNIFORM_FLOAT, 3);
#endif
#endif
    }

    void Shutdown() {
#ifdef MD_OPENGL43_ENABLED
        if (!init_) return;
        for (int k = 0; k < NUM_CASCADES; ++k) {
            if (fbo_[k])        glDeleteFramebuffers(1, &fbo_[k]);
            if (shadow_tex_[k]) glDeleteTextures(1,    &shadow_tex_[k]);
        }
#ifdef USE_SDL3
        MdUnloadShader(shadow_shader_);
#else
        UnloadShader(shadow_shader_);
#endif
        shadow_cull_cs_.Unload();
        shadow_vis_buf_.Shutdown();
        shadow_ind_buf_.Shutdown();
        init_ = false;
#endif
    }

private:
    ShadowSystem() = default;

    bool          init_    = false;
    unsigned int  fbo_[NUM_CASCADES]        = {};
    unsigned int  shadow_tex_[NUM_CASCADES] = {};
#ifdef USE_SDL3
    MdShader      shadow_shader_;
#else
    Shader        shadow_shader_ = {};
#endif
    ComputeShader shadow_cull_cs_;
    SSBO          shadow_vis_buf_;
    SSBO          shadow_ind_buf_;
#ifdef USE_SDL3
    MdMesh        mesh_;
#else
    Mesh          mesh_    = {};
#endif
    int           mesh_idx_ = 0;
    Vector3       cam_pos_ = {};
    int  loc_lightVP_  = -1;
    int  loc_svCamPos_ = -1, loc_svMaxDist_ = -1, loc_svTotal_ = -1;
};
