#version 330 core
// billboard.frag — sprite atlas lookup + alpha-test for Flare pixel-art sprites.
// Alpha-test instead of alpha-blend avoids depth-sort requirement.

in vec2 v_uv;
in vec4 v_tint;

uniform sampler2D u_atlas;
uniform float     u_alpha_threshold;   // typical 0.5 for pixel art

out vec4 frag_color;

void main() {
    vec4 c = texture(u_atlas, v_uv);
    if (c.a < u_alpha_threshold) discard;
    frag_color = c * v_tint;
}
