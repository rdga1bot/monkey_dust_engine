#version 430 core
#extension GL_ARB_shader_draw_parameters : require
// Raylib standard attribute locations:
layout(location = 0) in vec3  aPos;
layout(location = 2) in vec3  aNormal;
layout(location = 6) in vec4  aWeights;   // SHADER_LOC_VERTEX_BONEWEIGHTS
layout(location = 7) in uvec4 aJoints;    // SHADER_LOC_VERTEX_BONEIDS

layout(std430, binding = 0) readonly buffer TransformBuf { vec4 xzyr[]; };
layout(std430, binding = 1) readonly buffer VisibleBuf   { uint visible_indices[]; };
layout(std430, binding = 3) readonly buffer FactionBuf   { uint factions[]; };
layout(std430, binding = 4) readonly buffer FinalBonesBuf {
    mat4 final_bones[]; // [MAX_ANIMATED_NPC * MAX_BONES]
};

uniform mat4 viewProj;
uniform vec4 factionColors[4];
uniform float fogNear;
uniform float fogFar;
uniform vec3  cameraPos;
uniform vec3  fogColor;

out vec4 vColor;
out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    uint instanceIdx = visible_indices[gl_BaseInstanceARB + gl_InstanceID];
    vec4 data = xzyr[instanceIdx];
    float x    = data.x, z = data.y, y = data.z, rotY = data.w;

    float s = sin(rotY), c = cos(rotY);
    mat4 model = mat4(
         c,   0.0, -s,  0.0,
         0.0, 1.0,  0.0, 0.0,
         s,   0.0,  c,  0.0,
         x,   y + 0.9, z, 1.0
    );

    // Skeletal skinning — bone 0 = identity, so meshes without weights render correctly.
    // When no boneWeights attribute is present, GPU supplies vec4(0,0,0,1); joint 0 maps to
    // identity bone matrix, giving aPos unchanged.
    uint boneBase = (instanceIdx < 500u) ? instanceIdx * 128u : 0u;
    vec3 skinnedPos =
        aWeights.x * (final_bones[boneBase + aJoints.x] * vec4(aPos, 1.0)).xyz +
        aWeights.y * (final_bones[boneBase + aJoints.y] * vec4(aPos, 1.0)).xyz +
        aWeights.z * (final_bones[boneBase + aJoints.z] * vec4(aPos, 1.0)).xyz +
        aWeights.w * (final_bones[boneBase + aJoints.w] * vec4(aPos, 1.0)).xyz;

    vec4 worldPos4 = model * vec4(skinnedPos, 1.0);
    gl_Position = viewProj * worldPos4;
    vWorldPos   = worldPos4.xyz;
    // Normal transform: model has only rotation+translation → mat3(model) suffices
    vNormal = normalize(mat3(model) * aNormal);

    uint fid = min(factions[instanceIdx], 3u);
    vColor = factionColors[fid];
}
