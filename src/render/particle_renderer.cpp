#include <monkey_dust/render/particle_renderer.h>

#ifdef MD_OPENGL43_ENABLED

void ParticleRenderer::Init() {
    particle_vbo_ = rlLoadVertexBuffer(NULL, MAX_PARTICLES * 20, true);
    particle_vao_ = rlLoadVertexArray();
    rlEnableVertexArray(particle_vao_);
    rlEnableVertexBuffer(particle_vbo_);
    rlSetVertexAttribute(0, 3, RL_FLOAT,         false, 20, 0);
    rlSetVertexAttribute(1, 1, RL_FLOAT,         false, 20, 12);
    rlSetVertexAttribute(2, 4, RL_UNSIGNED_BYTE, true,  20, 16);
    rlEnableVertexAttribute(0);
    rlEnableVertexAttribute(1);
    rlEnableVertexAttribute(2);
    rlDisableVertexBuffer();
    rlDisableVertexArray();
    particle_shader_ = MdLoadShader("shaders/particle.vert", "shaders/particle.frag");
    loc_viewProj_ = MdGetLoc(particle_shader_, "viewProj");
    loc_camPos_   = MdGetLoc(particle_shader_, "cameraPos");
}

void ParticleRenderer::Draw(Matrix viewProj, Vector3 cam_pos) {
    static ParticleVertex verts[MAX_PARTICLES];
    int count = ParticleSoA::Get().BuildVertices(verts, MAX_PARTICLES);
    if (count <= 0) return;
    rlUpdateVertexBuffer(particle_vbo_, verts, count * 20, 0);
    glEnable(GL_PROGRAM_POINT_SIZE);
    MdUseShader(particle_shader_);
    MdSetMat4(loc_viewProj_, &viewProj.m0);
    float cp[3] = { cam_pos.x, cam_pos.y, cam_pos.z };
    MdSetVec3(loc_camPos_, cp);
    rlEnableVertexArray(particle_vao_);
    rlEnableVertexBuffer(particle_vbo_);
    glDrawArrays(GL_POINTS, 0, count);
    rlDisableVertexBuffer();
    rlDisableVertexArray();
    MdStopShader();
    glDisable(GL_PROGRAM_POINT_SIZE);
}

void ParticleRenderer::Shutdown() {
    MdUnloadShader(particle_shader_);
    rlUnloadVertexArray(particle_vao_);
    rlUnloadVertexBuffer(particle_vbo_);
}

#endif // MD_OPENGL43_ENABLED
