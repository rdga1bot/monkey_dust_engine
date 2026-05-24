#pragma once
// Portable camera abstraction — replaces Camera3D in rendering code (M6+).
// Rule M-C: use Vec3/Mat4 (math_types.h), not Vector3/Matrix directly.
//
// ViewMatrix()/ProjMatrix(): platform-neutral via mat4_lookat/mat4_perspective.
// Raylib adapters (ToRaylib/FromRaylib/GetViewProjRaylib/FrustumPlanes):
//   activated when RAYLIB_H is defined (non-USE_SDL3 legacy path only).

#include <monkey_dust/platform/math_types.h>

// GlmToRaylibMat — only meaningful in USE_GLM builds (glm::mat4 → Raylib Matrix).
#if defined(RAYLIB_H) && defined(USE_GLM)
inline Matrix GlmToRaylibMat(const glm::mat4& m) {
    Matrix r;
    r.m0 =m[0][0]; r.m4 =m[1][0]; r.m8 =m[2][0]; r.m12=m[3][0];
    r.m1 =m[0][1]; r.m5 =m[1][1]; r.m9 =m[2][1]; r.m13=m[3][1];
    r.m2 =m[0][2]; r.m6 =m[1][2]; r.m10=m[2][2]; r.m14=m[3][2];
    r.m3 =m[0][3]; r.m7 =m[1][3]; r.m11=m[2][3]; r.m15=m[3][3];
    return r;
}
#endif

struct MdCamera {
    Vec3  pos;
    Vec3  target;
    Vec3  up;
    float fovy;   // degrees

    Mat4 ViewMatrix() const { return mat4_lookat(pos, target, up); }
    // Correct VP product for the active backend.
    // SDL_GPU (column-major, depth [0,1]): clip = Proj * View * pos → P * V.
    // Raylib/OpenGL (row-major convention): V * P was correct historically.
    Mat4 ViewProjMatrix(float aspect) const {
#ifdef MD_SDL_GPU
        return mat4_mul(ProjMatrix(aspect), ViewMatrix());
#else
        return mat4_mul(ViewMatrix(), ProjMatrix(aspect));
#endif
    }

    Mat4 ProjMatrix(float aspect) const {
        static constexpr float DEG2R = 0.01745329251f;
        return mat4_perspective(fovy * DEG2R, aspect, 0.5f, 4000.f);
    }

    inline void CamPosToArr(float out[3]) const {
        out[0] = pos.x; out[1] = pos.y; out[2] = pos.z;
    }

#ifdef RAYLIB_H
    Camera3D ToRaylib() const {
        Camera3D r   = {};
        r.position   = { pos.x,    pos.y,    pos.z    };
        r.target     = { target.x, target.y, target.z };
        r.up         = { up.x,     up.y,     up.z     };
        r.fovy       = fovy;
        r.projection = CAMERA_PERSPECTIVE;
        return r;
    }

    static MdCamera FromRaylib(const Camera3D& c) {
        MdCamera m;
        m.pos    = { c.position.x, c.position.y, c.position.z };
        m.target = { c.target.x,   c.target.y,   c.target.z   };
        m.up     = { c.up.x,       c.up.y,       c.up.z       };
        m.fovy   = c.fovy;
        return m;
    }

    inline Matrix GetViewProjRaylib(float aspect) const {
        static constexpr float DEG2R = 0.01745329251f;
#  ifdef USE_GLM
        return GlmToRaylibMat(ViewMatrix() * ProjMatrix(aspect));
#  else
        return MatrixMultiply(
            MatrixLookAt(pos, target, up),
            MatrixPerspective(fovy * DEG2R, aspect, 0.5f, 4000.f)
        );
#  endif
    }

#endif // RAYLIB_H

    // Portable frustum plane extraction — works in any build.
    // Fills 4 planes (left, right, top, bottom) as vec4[4] for the cull shader.
    inline void FrustumPlanes(float aspect, float out[16]) const {
        Mat4 vp = ViewProjMatrix(aspect);
        const float* m = mat4_ptr(vp);
        // Gribb & Hartmann: row3 ± rowN from column-major float[16].
        out[ 0]=m[3]+m[0]; out[ 1]=m[7]+m[4]; out[ 2]=m[11]+m[ 8]; out[ 3]=m[15]+m[12]; // left
        out[ 4]=m[3]-m[0]; out[ 5]=m[7]-m[4]; out[ 6]=m[11]-m[ 8]; out[ 7]=m[15]-m[12]; // right
        out[ 8]=m[3]-m[1]; out[ 9]=m[7]-m[5]; out[10]=m[11]-m[ 9]; out[11]=m[15]-m[13]; // top
        out[12]=m[3]+m[1]; out[13]=m[7]+m[5]; out[14]=m[11]+m[ 9]; out[15]=m[15]+m[13]; // bottom
    }
};

// Project a world-space point to screen pixels (column-major VP, Y-down screen).
// Portable: uses mat4_ptr — no Raylib types required.
inline Vec2 MdWorldToScreen(Vec3 world, const MdCamera& cam, int sw, int sh) {
    float aspect = (sw > 0 && sh > 0) ? (float)sw / (float)sh : 1.f;
    Mat4         vp = cam.ViewProjMatrix(aspect);
    const float*  m = mat4_ptr(vp);
    float cx = m[0]*world.x + m[4]*world.y + m[8] *world.z + m[12];
    float cy = m[1]*world.x + m[5]*world.y + m[9] *world.z + m[13];
    float cw = m[3]*world.x + m[7]*world.y + m[11]*world.z + m[15];
    if (fabsf(cw) < 1e-6f) return {};
    return { (cx/cw + 1.f) * 0.5f * (float)sw,
             (1.f - cy/cw) * 0.5f * (float)sh };
}
