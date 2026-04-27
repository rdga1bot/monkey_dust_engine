#version 430 core
// Raylib standard attribute locations
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;

uniform mat4 mvp;       // Raylib: model-view-projection
uniform mat4 matModel;  // Raylib: model matrix

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = matModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    // No non-uniform scale → mat3(matModel) is valid normal matrix
    vNormal   = normalize(mat3(matModel) * aNormal);
    vUV       = aTexCoord;
    gl_Position = mvp * vec4(aPos, 1.0);
}
