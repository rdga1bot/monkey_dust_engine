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

    // Vertex UBO: mat4 vp (64 bytes) + float uv_scale + float _pad[3] = 80 bytes.
    struct TerrainVertUBO {
        float vp[16];       // column-major 4×4 MVP
        float uv_scale;     // world UV repeat factor (e.g. 0.125 = 8m tile)
        float _pad[3];
        // 80 bytes total
    };

    bool Init();
    void Shutdown();

    // Load 4 diffuse textures for splat blending.
    // Returns true if all 4 loaded; false if any failed (terrain still renders
    // using 1×1 white fallback textures — colours come from the frag shader).
    bool InitTextures(const char* grass, const char* rock,
                      const char* dirt,  const char* bark);

    // Draw chunk inside an already-open GpuCommandBuffer colour pass.
    // vp16: column-major Mat4 (16 floats = 64 bytes).
    // For code that uses GpuCommandBuffer (demo):
    void Draw(GpuCommandBuffer& cb,
              const TerrainChunk& chunk,
              const float* vp16,
              const SunParams& sun,
              float uv_scale = 0.125f);

    // For code that uses raw SDL pointers (game main loop):
    void DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                 const TerrainChunk& chunk,
                 const float* vp16,
                 const SunParams& sun,
                 float uv_scale = 0.125f);

    bool IsReady() const;

private:
    GpuPipeline pipeline_;
    GpuTexture  tex_[4];      // 0=grass 1=rock 2=dirt 3=bark
    bool        tex_loaded_ = false;

#ifdef MD_SDL_GPU
    // 1×1 white fallback used when a texture fails to load.
    SDL_GPUTexture* fallback_tex_     = nullptr;
    SDL_GPUSampler* fallback_sampler_ = nullptr;

    // Fill a SDL_GPUTextureSamplerBinding[4] using loaded textures or fallbacks.
    void FillSamplerBindings(SDL_GPUTextureSamplerBinding out[4]) const;
#endif
};
