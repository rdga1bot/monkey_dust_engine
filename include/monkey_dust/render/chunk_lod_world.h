#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>
#include <cstdint>

// Chunklod honest-retest (docs/TERRAIN_CHUNKLOD_PORT_PLAN.md, reopened --
// see the plan file for why the 2026-08-26 rejection of the original
// Phase 5 spike wasn't the final word). Streams pre-baked (tools/
// chunklod_bake --bake-all) zone meshes within a radius of the camera and
// draws them through the same shared G-buffer pipeline (shaders/
// chunk_lod.vert + shaders/terrain_gbuffer_mini.frag, unmodified) --
// shaded by the same TerrainShadingProjected resolve pass real terrain
// uses, so it automatically inherits whatever TS_ComputeGroundAlbedo
// formula is current (verified live via a pixel-diff A/B against
// TerrainQuadtreeRenderer, see the reopened plan's Фаза 1).
//
// Real per-zone distance-LOD (the Fаза 2 addition that closes the
// original spike's biggest, explicitly-scoped-out gap): each zone was
// baked at kNumLodLevels discrete activation levels (tools/chunklod_bake,
// kLodActivationLevels) from the SAME error-annotated heightfield Ulrich's
// real algorithm produces -- not a full persistent per-node tree with
// chunklod.cpp's continuous compute_lod/can_split, but a genuine,
// honestly-scoped simplification of it: SelectLod() below applies the
// same log2(distance) falloff compute_lod() uses, just picking one of
// kNumLodLevels precomputed whole-zone meshes instead of splitting
// individual nodes. Far zones now really do drop to a fraction of their
// near-camera triangle count (measured: 4474->798->50->10 tris across
// the 4 levels for the South Hive zone) -- this, not shading, was the
// real reason the original "always finest" spike ran at 30-40 FPS.
//
// Zone world placement matches TerrainQuadtree's own convention exactly
// (terrain_quadtree.h: "Quadtree roots are aligned to real Kenshi zones,
// CHUNK_SIZE=460.8m each") -- zone (zx,zy)'s mesh-local [0,CHUNK_SIZE]
// square places at world origin (zx*CHUNK_SIZE, zy*CHUNK_SIZE).
class ChunkLodWorld {
public:
    static constexpr int kMaxLoadedZones = 81;  // radius<=4 -> 9x9=81 zones, generous headroom
    // Must match tools/chunklod_bake's kNumLodLevels exactly -- the bake
    // tool and this loader agree on the file-naming/level contract
    // (zone_<zx>_<zy>_lod<i>.mesh, i=0 finest..kNumLodLevels-1 coarsest)
    // but there is no shared header between the two standalone binaries
    // to enforce this at compile time.
    static constexpr int kNumLodLevels = 4;

    bool Init(SDL_GPUDevice* dev, const char* bake_dir, int atlas_zones = 64);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Loads zones within `radius_zones` (Chebyshev distance) of the zone
    // containing (cam_x, cam_z) (world-space metres), unloads zones that
    // fell outside it, and re-selects each loaded zone's LOD level by real
    // distance from the camera to that zone's centre (SelectLod). Cheap to
    // call every frame: the zone add/remove scan only runs on a zone-
    // boundary crossing (as before), but the per-slot LOD re-check is a
    // handful of distance computations, not real streaming I/O -- disk
    // reads only happen on an actual LOD-level change for that zone.
    //
    // radius_zones is clamped to 4 internally (9x9=81 == kMaxLoadedZones) --
    // a larger value would silently under-render since the slot array can't
    // hold more; UpdateStreaming logs a warning if this clamp triggers.
    void UpdateStreaming(SDL_GPUDevice* dev, float cam_x, float cam_z, int radius_zones);

    void Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, const float* vp16);

    int LoadedZoneCount() const;
    int64_t LoadedTriangleCount() const;

    // Exposed for unit testing -- pure function of distance, no state.
    // Mirrors chunklod.cpp's compute_lod() log2(distance) falloff
    // (tmp_/chunklod_reference/chunklod.cpp:2145-2161) at whole-zone
    // granularity: doubling distance bands per LOD level, kLod0MaxDist
    // chosen so a camera inside the current zone or its immediate
    // neighbor sees the finest level.
    static int SelectLod(float dist_to_zone_center);

private:
    struct ZoneSlot {
        bool loaded = false;
        int zx = -1, zy = -1;
        int lod = -1;
        GpuStaticBuffer vbo, ibo;
        uint32_t index_count = 0;
    };
    ZoneSlot slots_[kMaxLoadedZones];
    char bake_dir_[256] = {};
    int atlas_zones_ = 64;
    int last_center_zx_ = -1000, last_center_zy_ = -1000;
    GpuPipeline pipeline_;
    bool ready_ = false;

    int FindSlot(int zx, int zy) const;
    bool LoadZoneMesh(SDL_GPUDevice* dev, int zx, int zy, int lod, ZoneSlot& slot);
};
#endif
