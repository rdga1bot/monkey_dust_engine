#version 430 core
layout(location = 0) in vec3  aPos;
layout(location = 1) in float aSize;
layout(location = 2) in vec4  aColor;

uniform mat4 viewProj;
uniform vec3 cameraPos;
out vec4 vColor;

void main() {
    gl_Position = viewProj * vec4(aPos, 1.0);
    float dist = length(aPos - cameraPos);
    gl_PointSize = clamp(aSize * 300.0 / max(dist, 1.0), 1.0, 64.0);
    vColor = aColor;
}
