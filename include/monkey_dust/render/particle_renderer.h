#pragma once
#include <monkey_dust/render/particle_soa.h>
#include "raylib.h"
#include "rlgl.h"
#ifdef MD_OPENGL43_ENABLED
#include "external/glad.h"
#endif

#ifdef MD_OPENGL43_ENABLED

class ParticleRenderer {
public:
    static ParticleRenderer& Get() {
        static ParticleRenderer inst;
        return inst;
    }

    void Init();
    void Draw(Matrix viewProj, Vector3 cam_pos);
    void Shutdown();

private:
    ParticleRenderer() = default;

    unsigned int particle_vbo_ = 0;
    unsigned int particle_vao_ = 0;
    Shader       particle_shader_ = {};
    int          loc_viewProj_ = -1;
    int          loc_camPos_   = -1;
};

#endif // MD_OPENGL43_ENABLED
