#pragma once

// Portable math type aliases.
// Default: Raylib types (no dependency change for existing code).
// Migration: compile with -DUSE_GLM to switch to glm — one flag, zero code changes.
// Rule M-C: use Vec3/Mat4 (not Vector3/Matrix) in all new files from Phase 22+.

#ifndef USE_GLM
#  include "raylib.h"
#  include "raymath.h"
   using Vec2 = Vector2;
   using Vec3 = Vector3;
   using Vec4 = Vector4;
   using Mat4 = Matrix;
   using Quat = Quaternion;
   inline Vec3 vec3_add  (Vec3 a, Vec3 b)    { return Vector3Add(a, b); }
   inline Vec3 vec3_sub  (Vec3 a, Vec3 b)    { return Vector3Subtract(a, b); }
   inline Vec3 vec3_scale(Vec3 v, float s)   { return Vector3Scale(v, s); }
   inline float vec3_dot (Vec3 a, Vec3 b)    { return Vector3DotProduct(a, b); }
   inline float vec3_len (Vec3 v)            { return Vector3Length(v); }
   inline Vec3 vec3_norm (Vec3 v)            { return Vector3Normalize(v); }
   inline Mat4 mat4_identity()               { return MatrixIdentity(); }
   inline Mat4 mat4_mul  (Mat4 a, Mat4 b)    { return MatrixMultiply(a, b); }
   inline Mat4 mat4_translate(float x, float y, float z) { return MatrixTranslate(x, y, z); }
   inline Mat4 mat4_rotate_y(float angle)    { return MatrixRotateY(angle); }
   inline Mat4 mat4_perspective(float fovy, float aspect, float near_z, float far_z)
                                             { return MatrixPerspective(fovy, aspect, near_z, far_z); }
#else
#  include <glm/glm.hpp>
#  include <glm/gtc/matrix_transform.hpp>
   using Vec2 = glm::vec2;
   using Vec3 = glm::vec3;
   using Vec4 = glm::vec4;
   using Mat4 = glm::mat4;
   using Quat = glm::quat;
   inline Vec3 vec3_add  (Vec3 a, Vec3 b)    { return a + b; }
   inline Vec3 vec3_sub  (Vec3 a, Vec3 b)    { return a - b; }
   inline Vec3 vec3_scale(Vec3 v, float s)   { return v * s; }
   inline float vec3_dot (Vec3 a, Vec3 b)    { return glm::dot(a, b); }
   inline float vec3_len (Vec3 v)            { return glm::length(v); }
   inline Vec3 vec3_norm (Vec3 v)            { return glm::normalize(v); }
   inline Mat4 mat4_identity()               { return glm::mat4(1.0f); }
   inline Mat4 mat4_mul  (Mat4 a, Mat4 b)    { return a * b; }
   inline Mat4 mat4_translate(float x, float y, float z)
                                             { return glm::translate(glm::mat4(1.0f), {x, y, z}); }
   inline Mat4 mat4_rotate_y(float angle)    { return glm::rotate(glm::mat4(1.0f), angle, {0.0f, 1.0f, 0.0f}); }
   inline Mat4 mat4_perspective(float fovy, float aspect, float near_z, float far_z)
                                             { return glm::perspective(fovy, aspect, near_z, far_z); }
#endif
