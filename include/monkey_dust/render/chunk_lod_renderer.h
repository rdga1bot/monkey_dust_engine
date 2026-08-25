#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <monkey_dust/render/gpu_hal.h>

// Chunklod runtime spike (Phase 4, docs/TERRAIN_CHUNKLOD_PORT_PLAN.md).
// Dual-run debug renderer ONLY -- draws one offline-baked test mesh
// (tools/chunklod_bake --write-mesh) into the SAME private G-buffer
// TerrainQuadtreeRenderer writes to, reusing shaders/terrain_gbuffer_mini.frag
// unchanged so the existing TerrainShadingProjected resolve pass shades it
// identically to real terrain -- the gate is geometry/GPU cost, not shading.
//
// Real vertex/index buffers (GpuStaticBuffer, uploaded once at LoadMesh()),
// unlike TerrainQuadtreeRenderer's vertex-buffer-less shared-IBO technique
// -- deliberately different, since testing THAT representation is the
// entire point of this class existing.
class ChunkLodRenderer {
public:
    bool Init(SDL_GPUDevice* dev);
    void Shutdown(SDL_GPUDevice* dev);
    bool IsReady() const { return ready_; }

    // Loads a mesh baked by `chunklod_bake --write-mesh <path>` (format:
    // u32 vcount, u32 icount, vcount * {pos.xyz, normal.xyz} float32,
    // icount * u32 indices). Returns false (and leaves any previously
    // loaded mesh in place) on read/format failure.
    bool LoadMesh(SDL_GPUDevice* dev, const char* path);
    bool HasMesh() const { return mesh_loaded_; }
    uint32_t TriangleCount() const { return index_count_ / 3; }

    // Draws the loaded mesh inside an already-open G-buffer render pass,
    // offset by origin_xyz (this spike's mesh has no real-world zone
    // placement -- docs/kenshi/03_reconciled_model.md leaves that origin
    // [UNKNOWN] -- so the caller picks a debug location).
    void Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
              const float* vp16, const float* origin_xyz);

private:
    GpuPipeline gbuffer_pipeline_;
    GpuStaticBuffer vbo_;
    GpuStaticBuffer ibo_;
    uint32_t index_count_ = 0;
    bool ready_ = false;
    bool mesh_loaded_ = false;
};
#endif
