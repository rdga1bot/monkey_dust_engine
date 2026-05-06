#include <monkey_dust/render/particle_renderer.h>

#if defined(MD_OPENGL43_ENABLED) || defined(MD_SDL_GPU)
#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#endif

void ParticleRenderer::Init() {
    // Vertex layout: pos(F3) at offset 0, size(F1) at 12, color(U8x4_NORM) at 16.
    // Matches ParticleVertex { float x,y,z,size; uint8_t r,g,b,a; } stride=20.
    GpuPipeline::Desc desc;
    desc.vert_path        = "shaders/particle.vert";
    desc.frag_path        = "shaders/particle.frag";
    desc.layout.stride    = 20;
    desc.layout.count     = 3;
    desc.layout.attribs[0] = { 0, 0,  GpuAttribFmt::F3 };        // pos
    desc.layout.attribs[1] = { 1, 12, GpuAttribFmt::F1 };        // size
    desc.layout.attribs[2] = { 2, 16, GpuAttribFmt::U8x4_NORM }; // color

    desc.raster.topology    = GpuTopology::POINTS;
    desc.raster.blend_enable = false;
    desc.raster.depth_test  = true;
    desc.raster.depth_write = false;  // particles don't write depth
    desc.raster.cull_back   = false;  // points have no faces
    desc.raster.point_size  = true;   // GL_PROGRAM_POINT_SIZE for distance-based radius

    pipeline_.Create(desc);

    loc_viewProj_ = pipeline_.UniformLoc("viewProj");
    loc_camPos_   = pipeline_.UniformLoc("cameraPos");

    vbuf_.Init(MAX_PARTICLES, 20);
}

void ParticleRenderer::Draw(Mat4 viewProj, Vec3 cam_pos) {
    // Build vertices from simulation SoA.
    void* ptr = vbuf_.MapWrite();
    if (!ptr) return;

    int count = ParticleSoA::Get().BuildVertices(
        static_cast<ParticleVertex*>(ptr), MAX_PARTICLES);
    vbuf_.Unmap();

    if (count <= 0) {
        vbuf_.Advance();
        return;
    }

    // Record and immediately execute draw commands.
    float cp[3] = { cam_pos.x, cam_pos.y, cam_pos.z };

    cmd_.BindPipeline(&pipeline_);
    cmd_.BindVertexBuffer(&vbuf_);
    cmd_.SetUniformMat4(loc_viewProj_, mat4_ptr(viewProj));
    cmd_.SetUniformVec3(loc_camPos_,   cp);
    cmd_.Draw((uint32_t)count);
    cmd_.EndPass();

    vbuf_.Advance();
}

void ParticleRenderer::Shutdown() {
    vbuf_.Shutdown();
    pipeline_.Destroy();
}

#endif // MD_OPENGL43_ENABLED || MD_SDL_GPU
