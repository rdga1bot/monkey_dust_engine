#pragma once
#include <monkey_dust/render/md_camera.h>
#include <monkey_dust/render/md_texture.h>
#include <cstdint>

namespace md::flare {

struct BillboardInstance {
    float    x, y, z;         // world center
    float    width, height;
    float    u0, v0, u1, v1;  // atlas UV rect [0,1]
    uint8_t  r, g, b, a;      // tint color
    uint8_t  atlas_idx = 0;   // which atlas slot [0..MAX_ATLAS-1]
};

constexpr int MAX_BILLBOARDS = 4096;

// Renders Flare 2D sprites as camera-facing quads in 3D world space.
// Supports up to MAX_ATLAS=4 sprite atlases per frame.
// Instances are sorted CPU-side by atlas_idx; one draw call per atlas group.
//
// Per-frame usage:
//   BeginFrame()                          — reset submitted list
//   Submit(inst) × N                      — queue instances
//   Render(cam, aspect)                   — ≤4 instanced draw calls
class BillboardRenderer {
public:
    static constexpr int MAX_ATLAS = 4;

    static BillboardRenderer& Get();

    void Init();
    void Shutdown();

    void BeginFrame();
    void Submit(const BillboardInstance& inst);
    void Render(const MdCamera& cam, float aspect);

    // Load atlas at slot idx (0..MAX_ATLAS-1). Default idx=0 for single-atlas compat.
    void LoadSpriteAtlas(const char* png_path, int idx = 0);
    void UnloadAllAtlases();
    void UnloadSpriteAtlas() { UnloadAllAtlases(); }  // backward-compat alias

    int SubmittedCount() const;
    int AtlasWidth(int idx = 0) const {
        return (idx >= 0 && idx < MAX_ATLAS) ? atlases_[idx].w : 0;
    }
    int AtlasHeight(int idx = 0) const {
        return (idx >= 0 && idx < MAX_ATLAS) ? atlases_[idx].h : 0;
    }

private:
    BillboardRenderer() = default;

    uint32_t vao_      = 0;
    uint32_t quad_vbo_ = 0;
    uint32_t inst_vbo_ = 0;
    uint32_t prog_     = 0;

    int loc_view_      = -1;
    int loc_proj_      = -1;
    int loc_cam_right_ = -1;
    int loc_cam_up_    = -1;
    int loc_alpha_thr_ = -1;

    MdTexture atlases_[MAX_ATLAS] = {};

    BillboardInstance instances_[MAX_BILLBOARDS];
    int               count_ = 0;

    bool init_ = false;
};

} // namespace md::flare
