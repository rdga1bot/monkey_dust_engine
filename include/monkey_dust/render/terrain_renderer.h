#pragma once
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/world/terrain_chunk.h>

// POM parameters — defined outside TerrainRenderer to avoid nested-class
// default-member-initializer parsing issues with GCC default arguments.
struct TerrainPomParams {
    float height_scale;  // world-space displacement depth (0.03=subtle, 0.12=deep)
    int   layers_min;    // layers when looking straight down (isometric view)
    int   layers_max;    // layers at grazing angle — Intel HD 520 budget: max 8

    TerrainPomParams() : height_scale(0.06f), layers_min(4), layers_max(8) {}
    TerrainPomParams(float hs, int lmin, int lmax)
        : height_scale(hs), layers_min(lmin), layers_max(lmax) {}
};

// TerrainRenderer — forward-rendering pass for a single TerrainChunk.
// Uses terrain_forward.vert/frag (single colour output, Lambertian lighting).
// POM variant: terrain_pom.vert/frag — Parallax Occlusion Mapping + self-shadow.
// For GBuffer deferred path see terrain.vert/frag (not yet wired).
class TerrainRenderer {
public:
    using PomParams = TerrainPomParams;  // convenience alias

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

    // Vertex UBO for POM pipeline: TerrainVertUBO (80 bytes) + cam_pos_ws (16 bytes) = 96 bytes.
    struct TerrainPomVertUBO {
        float vp[16];           // 64 bytes
        float world_origin_x;
        float world_origin_z;
        float world_to_uv;
        float _pad0;
        float cam_pos_ws[4];    // xyz=camera world position, w=unused
        // 96 bytes total
    };

    // Fragment UBO for POM pipeline: TerrainFragUBO (48 bytes) + pom_params (16 bytes) = 64 bytes.
    struct TerrainPomFragUBO {
        float sun_dir_str[4];   // 16 bytes
        float ambient[4];       // 16 bytes
        float world_params[4];  // 16 bytes
        float pom_params[4];    // x=height_scale, y=layers_min, z=layers_max, w=unused
        // 64 bytes total
    };

    // UBOs for GPU Synthesis pipeline (terrain_synth.vert/frag).
    struct SynthVertUBO {
        float vp[16];        // 64
        float world_ox;      // world origin X (metres)
        float world_oz;      // world origin Z (metres)
        float world_size;    // world extent (metres)
        float height_max;    // max height (metres)
        float cam_pos[4];    // xyz=camera, w=unused
        float grid_n;        // float(SYNTH_N)
        float _pad[3];
        // 112 bytes total
    };
    struct SynthFragUBO {
        float sun_dir_str[4];  // 16
        float ambient[4];      // 16
        float world_params[4]; // xy=world_ox/oz, z=world_to_uv, w=unused
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
    // world_origin_x/z: world coords mapped to UV centre (0.5).
    // world_to_uv: UV units per world-metre.  e.g. 1.0f/8000.0f shows 8km of Kenshi.
    void Draw(GpuCommandBuffer& cb,
              const TerrainChunk& chunk,
              const float* vp16,
              const SunParams& sun,
              float world_origin_x = 0.f,
              float world_origin_z = 0.f,
              float world_to_uv    = 1.f / 8000.f);

    // lod: 0=full 64×64, 1=32×32, 2=16×16, 3=8×8.
    // Use uniform lod for all chunks to avoid T-junctions.
    void DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                 const TerrainChunk& chunk,
                 const float* vp16,
                 const SunParams& sun,
                 float world_origin_x = 0.f,
                 float world_origin_z = 0.f,
                 float world_to_uv    = 1.f / 8000.f,
                 int   lod            = 0);

    // POM variant — call once after Init(). Then replace DrawRaw calls with DrawRawPOM.
    // detail_path: RGBA PNG where A=height [0,1]. Pass nullptr to use neutral fallback.
    bool InitPOM(const char* detail_path, const PomParams& p = PomParams());
    void ShutdownPOM();

    // Drop-in replacement for DrawRaw. Passes camera world position for tangent-space
    // view vector used in POM ray marching. Falls back to DrawRaw if POM not ready.
    // lod: 0=full 64×64, 1=32×32, 2=16×16, 3=8×8 (uniform across all chunks).
    void DrawRawPOM(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                    const TerrainChunk& chunk,
                    const float* vp16,
                    const SunParams& sun,
                    float cam_x, float cam_y, float cam_z,
                    float world_origin_x = 0.f,
                    float world_origin_z = 0.f,
                    float world_to_uv    = 1.f / 8000.f,
                    int   lod            = 0);

    bool IsReady()    const;
    bool IsPomReady() const;
    bool IsSynthReady() const;

    // GPU Synthesis: one draw call for the full world.
    // heights: R8 pixel data (height/height_max * 255), tex_w x tex_h.
    // Call once after terrain is loaded; uses existing kenshi colour overlay.
    bool InitSynth(const uint8_t* heights, int tex_w, int tex_h,
                   float world_ox, float world_oz,
                   float world_size, float height_max,
                   int grid_n = 512);
    void ShutdownSynth();
    void DrawSynth(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                   const float* vp16, const SunParams& sun,
                   float cam_x, float cam_y, float cam_z,
                   float world_origin_x, float world_origin_z,
                   float world_to_uv);

    // Batch API: hoists pipeline/UBO/sampler/IBO outside the per-chunk loop.
    // Reduces API calls from 6/chunk to 2/chunk for large worlds.
    // BeginRawBatch: bind pipeline, push vertex+frag UBO, bind sampler, bind shared IBO.
    // DrawRawChunk:  bind VBO + draw (2 calls per chunk).
    void BeginRawBatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                       const float* vp16, const SunParams& sun,
                       float world_origin_x, float world_origin_z,
                       float world_to_uv, int lod);
    void DrawRawChunk(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                      const TerrainChunk& chunk);

private:
    GpuPipeline pipeline_;
    GpuTexture  tex_colour_;   // Kenshi colour overlay (slot 0)
    GpuTexture  tex_[4];       // legacy splat (kept for InitTextures compat)
    bool        tex_loaded_ = false;

    // POM pipeline data
    GpuPipeline pom_pipeline_;
    GpuTexture  tex_detail_;   // RGB=detail tint, A=height [0,1]; tiling detail texture
    PomParams   pom_params_;
    bool        pom_loaded_ = false;

    // Shared LOD IBOs — one per LOD level, built in Init(), reused by all chunks.
    GpuStaticBuffer lod_ibo_shared_[TERRAIN_LOD_LEVELS];
    uint32_t        batch_idx_count_ = 0;  // set by BeginRawBatch

    // GPU Synthesis pipeline data
    GpuPipeline     synth_pipeline_;
    GpuStaticBuffer synth_ibo_;         // uint32 IBO for SYNTH_N × SYNTH_N grid
    GpuTexture      synth_hmap_;        // R8 heightmap texture
    int             synth_grid_n_  = 0;
    float           synth_world_ox_  = 0.f;
    float           synth_world_oz_  = 0.f;
    float           synth_world_size_= 0.f;
    float           synth_height_max_= 280.f;

#ifdef MD_SDL_GPU
    SDL_GPUTexture* fallback_tex_            = nullptr;
    SDL_GPUSampler* fallback_sampler_        = nullptr;
    SDL_GPUTexture* fallback_detail_tex_     = nullptr;
    SDL_GPUSampler* fallback_detail_sampler_ = nullptr;
    void FillSamplerBindings(SDL_GPUTextureSamplerBinding out[1]) const;
    void FillPomSamplerBindings(SDL_GPUTextureSamplerBinding out[2]) const;
#endif
};
