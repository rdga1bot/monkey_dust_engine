#pragma once
// FeatureScatter — loads and draws real Kenshi hand-authored scatter-object
// placement (features.dat, via private/md_features_parse.py --export ->
// game/data/features_scatter.txt), replacing the disabled noise-based
// PropGen_Build/ClutterGen_Build (see their own definitions for why they
// were turned off — this is the real replacement content pipeline those
// comments point at).
//
// Small enough (757 placements, 113 unique meshes) to load entirely once
// at startup and filter by camera distance every frame — no per-chunk
// baking/streaming needed, unlike the terrain/clutter systems.
#include <monkey_dust/render/prop_renderer.h>
#include <cstdint>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

class FeatureScatterSystem {
public:
    static constexpr int MAX_MESHES     = 128;
    static constexpr int MAX_PLACEMENTS = 900;
    static constexpr int MAX_DRAW_BATCH = PropRenderer::MAX_PROPS;

    static FeatureScatterSystem& Get();

    // Loads mesh_dir/<sanitized-name>.glb for every unique mesh referenced
    // in scatter_path, then the placement records themselves. Safe to call
    // once at startup; a missing scatter_path is a graceful no-op (Draw()
    // then draws nothing), same tolerance as PropGen_Build's own missing-
    // asset handling.
    bool Init(const char* scatter_path = "game/data/features_scatter.txt",
              const char* mesh_dir     = "game/data/props/features");
    void Shutdown();

    int PlacementCount() const { return placement_count_; }

    // Draws every placement within `radius` metres of (cam_x, cam_z),
    // batched per mesh type (one PropRenderer::DrawRaw call per mesh that
    // has >=1 visible instance this frame).
    void Draw(
#ifdef MD_SDL_GPU
        SDL_GPURenderPass*    rp,
        md::GpuCommandBufferHandle cmd,
#endif
        const float* vp16,
        const float* sun32,
        float        cam_x,
        float        cam_z,
        float        radius);

private:
    struct Placement {
        int16_t mesh_idx;
        float   x, y, z;
        float   sx, sy, sz;
        float   qw, qx, qy, qz;
    };

    PropRenderer renderers_[MAX_MESHES];
    char         mesh_names_[MAX_MESHES][64] = {};
    int          mesh_count_ = 0;

    Placement placements_[MAX_PLACEMENTS];
    int       placement_count_ = 0;

    int FindOrSkipMesh(const char* kenshi_mesh_path) const;
};
