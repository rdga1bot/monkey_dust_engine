#include <monkey_dust/render/particle_renderer.h>
#include "glad.h"

#ifdef MD_OPENGL43_ENABLED

void ParticleRenderer::Init() {
    glGenVertexArrays(1, &particle_vao_);
    glGenBuffers(1, &particle_vbo_);

    glBindVertexArray(particle_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, particle_vbo_);
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 20, nullptr, GL_DYNAMIC_DRAW);

    // layout: vec3 pos (0), float size (12), u8×4 color (16) — stride 20
    glVertexAttribPointer(0, 3, GL_FLOAT,         GL_FALSE, 20, (void*)0);
    glVertexAttribPointer(1, 1, GL_FLOAT,         GL_FALSE, 20, (void*)12);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  20, (void*)16);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    particle_shader_ = MdLoadShader("shaders/particle.vert", "shaders/particle.frag");
    loc_viewProj_ = MdGetLoc(particle_shader_, "viewProj");
    loc_camPos_   = MdGetLoc(particle_shader_, "cameraPos");
}

void ParticleRenderer::Draw(Mat4 viewProj, Vec3 cam_pos) {
    static ParticleVertex verts[MAX_PARTICLES];
    int count = ParticleSoA::Get().BuildVertices(verts, MAX_PARTICLES);
    if (count <= 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, particle_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * 20, verts);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_PROGRAM_POINT_SIZE);
    MdUseShader(particle_shader_);
    MdSetMat4(loc_viewProj_, mat4_ptr(viewProj));
    float cp[3] = { cam_pos.x, cam_pos.y, cam_pos.z };
    MdSetVec3(loc_camPos_, cp);

    glBindVertexArray(particle_vao_);
    glDrawArrays(GL_POINTS, 0, count);
    glBindVertexArray(0);

    MdStopShader();
    glDisable(GL_PROGRAM_POINT_SIZE);
}

void ParticleRenderer::Shutdown() {
    MdUnloadShader(particle_shader_);
    if (particle_vao_) { glDeleteVertexArrays(1, &particle_vao_); particle_vao_ = 0; }
    if (particle_vbo_) { glDeleteBuffers(1, &particle_vbo_);      particle_vbo_ = 0; }
}

#endif // MD_OPENGL43_ENABLED
