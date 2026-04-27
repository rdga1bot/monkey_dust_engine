#version 330 core

in vec2 v_uv;

uniform sampler2D u_atlas;

out vec4 frag_color;

void main() {
    vec4 c = texture(u_atlas, v_uv);
    if (c.a < 0.1) discard;
    frag_color = c;
}
