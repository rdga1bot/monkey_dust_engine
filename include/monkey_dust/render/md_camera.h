#pragma once
// Portable camera abstraction — replaces Camera3D in rendering code (M6+).
// Rule M-C: use Vec3/Mat4 (math_types.h), not Vector3/Matrix directly.
//
// ViewMatrix / ProjMatrix: under USE_GLM → glm; under !USE_GLM → Raylib raymath.
// ToRaylib() / FromRaylib(): adapters for Raylib functions still taking Camera3D
//   (BeginMode3D, GetWorldToScreen, DebugSystem, EditorTranslator — until M7/M10).
// GlmToRaylibMat(): M6 transition helper — converts GLM mat4 to Raylib Matrix
//   for functions still taking Raylib types (SetShaderValueMatrix, ParticleRenderer).

#include <monkey_dust/platform/math_types.h>
#include "raylib.h"    // Camera3D, Matrix, Vector3 always available

#ifdef USE_GLM
#  include <glm/glm.hpp>
#  include <glm/gtc/matrix_transform.hpp>
#  include <glm/gtc/type_ptr.hpp>

// Convert GLM mat4 → Raylib Matrix (both column-major; used by GetViewProjRaylib).
inline Matrix GlmToRaylibMat(const glm::mat4& m) {
    Matrix r;
    r.m0  = m[0][0]; r.m4  = m[1][0]; r.m8  = m[2][0]; r.m12 = m[3][0];
    r.m1  = m[0][1]; r.m5  = m[1][1]; r.m9  = m[2][1]; r.m13 = m[3][1];
    r.m2  = m[0][2]; r.m6  = m[1][2]; r.m10 = m[2][2]; r.m14 = m[3][2];
    r.m3  = m[0][3]; r.m7  = m[1][3]; r.m11 = m[2][3]; r.m15 = m[3][3];
    return r;
}
#endif

struct MdCamera {
    Vec3  pos;
    Vec3  target;
    Vec3  up;
    float fovy;   // degrees

    Mat4 ViewMatrix() const {
#ifdef USE_GLM
        return glm::lookAt(pos, target, up);
#else
        return MatrixLookAt(pos, target, up);
#endif
    }

    Mat4 ProjMatrix(float aspect) const {
        static constexpr float DEG2R = 0.01745329251f;
#ifdef USE_GLM
        return glm::perspective(fovy * DEG2R, aspect, 0.1f, 300.f);
#else
        return MatrixPerspective(fovy * DEG2R, aspect, 0.1f, 300.f);
#endif
    }

    // Convert to Raylib Camera3D — for BeginMode3D, GetWorldToScreen, debug overlays.
    Camera3D ToRaylib() const {
        Camera3D r     = {};
        r.position     = { pos.x,    pos.y,    pos.z    };
        r.target       = { target.x, target.y, target.z };
        r.up           = { up.x,     up.y,     up.z     };
        r.fovy         = fovy;
        r.projection   = CAMERA_PERSPECTIVE;
        return r;
    }

    // Construct MdCamera from Raylib Camera3D — for editor camera conversion.
    static MdCamera FromRaylib(const Camera3D& c) {
        MdCamera m;
        m.pos    = { c.position.x, c.position.y, c.position.z };
        m.target = { c.target.x,   c.target.y,   c.target.z   };
        m.up     = { c.up.x,       c.up.y,        c.up.z       };
        m.fovy   = c.fovy;
        return m;
    }

    // Camera position as float[3] — platform-neutral (replaces USE_SDL3 ifdef pair).
    inline void CamPosToArr(float out[3]) const {
        out[0] = pos.x; out[1] = pos.y; out[2] = pos.z;
    }

    // View-projection matrix as Raylib Matrix (column-major, same layout as glm::mat4).
    // Use (const float*)&result to pass raw floats to MdSetMat4.
    inline Matrix GetViewProjRaylib(float aspect) const {
        static constexpr float DEG2R = 0.01745329251f;
#ifdef USE_GLM
        return GlmToRaylibMat(ViewMatrix() * ProjMatrix(aspect));
#else
        return MatrixMultiply(
            MatrixLookAt(pos, target, up),
            MatrixPerspective(fovy * DEG2R, aspect, 0.1f, 300.f)
        );
#endif
    }

    // 4 view-frustum planes packed as float[16] (left/right/top/bottom × xyzw).
    inline void FrustumPlanes(float aspect, float out[16]) const {
        Matrix vp = GetViewProjRaylib(aspect);
        out[ 0]=vp.m3+vp.m0;  out[ 1]=vp.m7+vp.m4;  out[ 2]=vp.m11+vp.m8;  out[ 3]=vp.m15+vp.m12;
        out[ 4]=vp.m3-vp.m0;  out[ 5]=vp.m7-vp.m4;  out[ 6]=vp.m11-vp.m8;  out[ 7]=vp.m15-vp.m12;
        out[ 8]=vp.m3-vp.m1;  out[ 9]=vp.m7-vp.m5;  out[10]=vp.m11-vp.m9;  out[11]=vp.m15-vp.m13;
        out[12]=vp.m3+vp.m1;  out[13]=vp.m7+vp.m5;  out[14]=vp.m11+vp.m9;  out[15]=vp.m15+vp.m13;
    }
};

