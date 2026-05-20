#pragma once
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/world/terrain_chunk.h>

// TerrainRenderer — forward-rendering pass for a single TerrainChunk.
// Uses terrain_forward.vert/frag (single colour output, Lambertian lighting).
// For GBuffer deferred path see terrain.vert/frag (not yet wired).
class TerrainRenderer {
public:
    struct SunParams {
        float dir[3];      // world-space normalised direction
        float strength;    // diffuse multiplier
        float ambient[3];  // ambient colour
        float _pad;
        // 32 bytes total — pushed as fragment uniform slot 0

        static SunParams Default() {
            SunParams s;
            s.dir[0] = 0.57f; s.dir[1] = 0.57f; s.dir[2] = 0.57f;
            s.strength   = 1.2f;
            s.ambient[0] = 0.18f; s.ambient[1] = 0.20f; s.ambient[2] = 0.26f;
            s._pad       = 0.f;
            return s;
        }
    };

    // Vertex UBO: mat4 vp (64 bytes) + world params (16 bytes) = 80 bytes.
    struct TerrainVertUBO {
        float vp[16];           // column-major 4×4 MVP
        float world_origin_x;   // world X → UV 0.5
        float world_origin_z;   // world Z → UV 0.5
        float world_to_uv;      // UV per metre (1 / kenshi_view_metres)
        float _pad;
        // 80 bytes total
    };

    // Fragment UBO: sun(32 bytes) + world_params(16 bytes) = 48 bytes.
    struct TerrainFragUBO {
        float sun_dir_str[4];   // xyz=dir, w=strength
        float ambient[4];       // xyz=colour, w=unused
        float world_params[4];  // xy=origin_xz, z=world_to_uv, w=unused
        // 48 bytes total
    };

    bool Init();
    void Shutdown();

    // Load Kenshi stitched colour overlay (md_terrain.png, 4096×4096).
    bool InitKenshiOverlay(const char* path);

    // Legacy 4-texture splat — kept for non-Kenshi terrain; no-op when Kenshi overlay loaded.
    bool InitTextures(const char* grass, const char* rock,
                      const char* dirt,  const char* bark);

    // Draw chunk inside an already-open GpuCommandBuffer colour pass.
    // vp16: column-major Mat4 (16 floats = 64 bytes).
    // For code that uses GpuCommandBuffer (demo):
    // world_origin_x/z: world coords mapped to UV centre (0.5).
    // world_to_uv: UV units per world-metre.  e.g. 1.0f/8000.0f shows 8km of Kenshi.
    void Draw(GpuCommandBuffer& cb,
              const TerrainChunk& chunk,
              const float* vp16,
              const SunParams& sun,
              float world_origin_x = 0.f,
              float world_origin_z = 0.f,
              float world_to_uv    = 1.f / 8000.f);

    void DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                 const TerrainChunk& chunk,
                 const float* vp16,
                 const SunParams& sun,
                 float world_origin_x = 0.f,
                 float world_origin_z = 0.f,
                 float world_to_uv    = 1.f / 8000.f);

    bool IsReady() const;

private:
    GpuPipeline pipeline_;
    GpuTexture  tex_colour_;   // Kenshi colour overlay (slot 0)
    GpuTexture  tex_[4];       // legacy splat (kept for InitTextures compat)
    bool        tex_loaded_ = false;

#ifdef MD_SDL_GPU
    SDL_GPUTexture* fallback_tex_     = nullptr;
    SDL_GPUSampler* fallback_sampler_ = nullptr;
    void FillSamplerBindings(SDL_GPUTextureSamplerBinding out[1]) const;
#endif
};
