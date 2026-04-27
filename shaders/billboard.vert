#version 330 core
// billboard.vert — face-camera quad for Flare 2D sprites in 3D world.
// Each billboard is 4 vertices; camera_right/up expand the quad in world space.

layout(location = 0) in vec2 a_quad;       // local quad corner [-1,1]
layout(location = 1) in vec3 a_world_pos;  // instance: world center (XYZ)
layout(location = 2) in vec2 a_size;       // instance: width, height in world units
layout(location = 3) in vec4 a_uv_rect;   // instance: u0,v0,u1,v1 in atlas [0,1]
layout(location = 4) in vec4 a_tint;      // instance: RGBA [0,1]

uniform mat4 u_view;
uniform mat4 u_proj;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;

out vec2 v_uv;
out vec4 v_tint;

void main() {
    // Expand from world center using camera basis vectors.
    vec3 world = a_world_pos
               + u_camera_right * a_quad.x * a_size.x * 0.5
               + u_camera_up    * a_quad.y * a_size.y * 0.5;
    gl_Position = u_proj * u_view * vec4(world, 1.0);

    // Map quad corner [-1,1] → atlas UV via lerp.
    vec2 t = (a_quad + 1.0) * 0.5;   // [0,1]
    v_uv   = mix(a_uv_rect.xy, a_uv_rect.zw, t);
    v_tint = a_tint;
}
