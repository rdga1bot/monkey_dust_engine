#version 430 core
#extension GL_ARB_shader_draw_parameters : require
layout(location = 0) in vec3  aPos;
layout(location = 6) in vec4  aWeights;
layout(location = 7) in uvec4 aJoints;

layout(std430, binding = 0) readonly buffer TransformBuf  { vec4 xzyr[]; };
layout(std430, binding = 6) readonly buffer ShadowVisBuf  { uint shadow_visible[]; };
layout(std430, binding = 4) readonly buffer FinalBonesBuf { mat4 final_bones[]; };

uniform mat4 lightViewProj;

void main() {
    uint instanceIdx = shadow_visible[gl_BaseInstanceARB + gl_InstanceID];
    vec4 data = xzyr[instanceIdx];
    float x = data.x, z = data.y, y = data.z, rotY = data.w;

    float s = sin(rotY), c = cos(rotY);
    mat4 model = mat4(
         c,   0.0, -s,  0.0,
         0.0, 1.0,  0.0, 0.0,
         s,   0.0,  c,  0.0,
         x,   y + 0.9, z, 1.0
    );

    uint boneBase = (instanceIdx < 500u) ? instanceIdx * 128u : 0u;
    vec3 skinnedPos =
        aWeights.x * (final_bones[boneBase + aJoints.x] * vec4(aPos, 1.0)).xyz +
        aWeights.y * (final_bones[boneBase + aJoints.y] * vec4(aPos, 1.0)).xyz +
        aWeights.z * (final_bones[boneBase + aJoints.z] * vec4(aPos, 1.0)).xyz +
        aWeights.w * (final_bones[boneBase + aJoints.w] * vec4(aPos, 1.0)).xyz;

    gl_Position = lightViewProj * model * vec4(skinnedPos, 1.0);
}
