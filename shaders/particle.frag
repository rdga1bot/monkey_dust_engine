#version 430 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float r = dot(coord, coord);
    if (r > 1.0) discard;
    float alpha = vColor.a * (1.0 - r);
    fragColor = vec4(vColor.rgb, alpha);
}
