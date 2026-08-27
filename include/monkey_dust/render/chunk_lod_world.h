#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>
#include <cstdint>

// Chunklod Phase 5 runtime spike (docs/TERRAIN_CHUNKLOD_PORT_PLAN.md).
// Streams pre-baked (tools/chunklod_bake --bake-all) zone meshes within a
// radius of the camera and draws them through the same shared G-buffer
// pipeline ChunkLodRenderer uses (shaders/chunk_lod.vert +
// shaders/terrain_gbuffer_mini.frag, unmodified).
//
// NOT a full chunklod runtime: each zone is a single, fixed-resolution
// baked mesh (whatever --max-error the bake used) with no per-node
// distance-LOD switching -- that needs the whole activation-level node
// tree plus chunklod.cpp's compute_lod/can_split, explicitly out of scope
// for this A/B comparison spike against TerrainQuadtreeRenderer. A zone
// loads/unloads as a whole the moment it crosses the streaming radius.
//
// Zone world placement matches TerrainQuadtree's own convention exactly
// (terrain_quadtree.h: "Quadtree roots are aligned to real Kenshi zones,
// CHUNK_SIZE=460.8m each") -- zone (zx,zy)'s mesh-local [0,CHUNK_SIZE]
// square places at world origin (zx*CHUNK_SIZE, zy*CHUNK_SIZE).
class ChunkLodWorld {
public:
    static constexpr int kMaxLoadedZones = 81;  // radius<=4 -> 9x9=81 zones, generous headroom

    bool Init(SDL_GPUDevice* dev, const char* bake_dir, int atlas_zones = 64);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Loads zones within `radius_zones` (Chebyshev distance) of the zone
    // containing (cam_x, cam_z) (world-space metres), unloads zones that
    // fell outside it. Cheap to call every frame -- no-ops when the
    // camera hasn't crossed a zone boundary since the last call.
    //
    // radius_zones is clamped to 4 internally (9x9=81 == kMaxLoadedZones) --
    // a larger value would silently under-render since the slot array can't
    // hold more; UpdateStreaming logs a warning if this clamp triggers.
    void UpdateStreaming(SDL_GPUDevice* dev, float cam_x, float cam_z, int radius_zones);

    void Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, const float* vp16);

    int LoadedZoneCount() const;
    int64_t LoadedTriangleCount() const;

private:
    struct ZoneSlot {
        bool loaded = false;
        int zx = -1, zy = -1;
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
    bool LoadZoneMesh(SDL_GPUDevice* dev, int zx, int zy, ZoneSlot& slot);
};
#endif
