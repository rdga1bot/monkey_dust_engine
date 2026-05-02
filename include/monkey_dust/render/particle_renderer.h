#pragma once
#include <monkey_dust/render/particle_soa.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/platform/math_types.h>

#ifdef MD_OPENGL43_ENABLED

// ParticleRenderer — point-sprite particle system.
// Migrated to GpuPipeline + GpuVertexBuffer + GpuCommandBuffer (Action 3).
// Previously used raw OpenGL (VAO/VBO + MdShader + glEnable calls directly).
class ParticleRenderer {
public:
    static ParticleRenderer& Get() {
        static ParticleRenderer inst;
        return inst;
    }

    void Init();
    void Draw(Mat4 viewProj, Vec3 cam_pos);
    void Shutdown();

private:
    ParticleRenderer() = default;

    GpuPipeline      pipeline_;
    GpuVertexBuffer  vbuf_;
    GpuCommandBuffer cmd_;

    int loc_viewProj_ = -1;
    int loc_camPos_   = -1;
};

#endif // MD_OPENGL43_ENABLED
