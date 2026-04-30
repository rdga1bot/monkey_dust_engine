#pragma once
#include <monkey_dust/render/md_camera.h>
#include <monkey_dust/render/md_texture.h>
#include <cstdint>

namespace md::flare {

struct BillboardInstance {
    float    x, y, z;         // world center
    float    width;
    float    height;
    float    u0, v0, u1, v1;  // atlas UV rect [0,1]
    uint8_t  r, g, b, a;      // tint color
};

constexpr int MAX_BILLBOARDS = 4096;

// Renders Flare 2D sprites as camera-facing quads in 3D world space.
// One shared sprite-atlas texture for all billboards per frame.
// Uses alpha-test (discard) — no depth sort required.
//
// Per-frame usage:
//   BeginFrame()              — reset submitted list
//   Submit(inst) × N          — queue instances
//   Render(cam)               — one instanced draw call
class BillboardRenderer {
public:
    static BillboardRenderer& Get();

    void Init();
    void Shutdown();

    void BeginFrame();
    void Submit(const BillboardInstance& inst);
    void Render(const MdCamera& cam, float aspect);

    void LoadSpriteAtlas(const char* png_path);
    void UnloadSpriteAtlas();

    int SubmittedCount() const;
    int AtlasWidth()     const { return atlas_.w; }
    int AtlasHeight()    const { return atlas_.h; }

private:
    BillboardRenderer() = default;

    // GPU objects
    uint32_t vao_     = 0;
    uint32_t quad_vbo_ = 0;   // 4 vertices, shared quad geometry
    uint32_t inst_vbo_ = 0;   // per-instance stream

    uint32_t prog_    = 0;
    int      loc_view_       = -1;
    int      loc_proj_       = -1;
    int      loc_cam_right_  = -1;
    int      loc_cam_up_     = -1;
    int      loc_alpha_thr_  = -1;

    MdTexture atlas_;

    BillboardInstance instances_[MAX_BILLBOARDS];
    int               count_ = 0;

    bool init_ = false;
};

} // namespace md::flare
