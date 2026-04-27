#version 330 core

// Shared quad corner [0,1]×[0,1]
layout(location = 0) in vec2  a_corner;

// Per-instance
layout(location = 1) in vec2  a_tile_pos;   // tile grid (col, row)
layout(location = 2) in vec4  a_uv_rect;    // (u0, v0, u1, v1) in atlas

uniform mat4  u_view;
uniform mat4  u_proj;
uniform vec2  u_tile_size;  // world-space tile footprint (x=width, y=depth)
uniform float u_y;          // ground level

out vec2 v_uv;

void main() {
    float tx = a_tile_pos.x;
    float ty = a_tile_pos.y;

    // Isometric diamond grid → flat world XZ (south-east layout)
    float wx = (tx - ty) * u_tile_size.x * 0.5;
    float wz = (tx + ty) * u_tile_size.y * 0.5;

    // Expand corner around the tile center
    vec3 world_pos = vec3(
        wx + (a_corner.x - 0.5) * u_tile_size.x,
        u_y,
        wz + (a_corner.y - 0.5) * u_tile_size.y
    );

    gl_Position = u_proj * u_view * vec4(world_pos, 1.0);

    v_uv = vec2(
        mix(a_uv_rect.x, a_uv_rect.z, a_corner.x),
        mix(a_uv_rect.y, a_uv_rect.w, a_corner.y)
    );
}
