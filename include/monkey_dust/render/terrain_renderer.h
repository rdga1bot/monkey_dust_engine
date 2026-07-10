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

    // Vertex UBO: mat4 vp (64) + world params (16) + cam_pos_ws (16) = 96 bytes.
    struct TerrainVertUBO {
        float vp[16];           // column-major 4×4 MVP
        float world_origin_x;   // world X → UV 0.5
        float world_origin_z;   // world Z → UV 0.5
        float world_to_uv;      // UV per metre (1 / kenshi_view_metres)
        float _pad;
        float cam_pos_ws[4];    // xyz=camera world position, w=unused (fog distance)
        // 96 bytes total
    };

    // Fragment UBO: sun(32) + world_params(16) + ground_layers_a/b(32) + fog_color_near(16)
    // + blend_layers(16) = 112 bytes.
    struct TerrainFragUBO {
        float sun_dir_str[4];     // xyz=dir, w=strength
        float ambient[4];         // xyz=colour, w=unused
        float world_params[4];    // xy=origin_xz, z=world_to_uv, w=unused
        float ground_layers_a[4]; // xyzw = base,slope,cliff,grass GroundTexLayer indices
        float ground_layers_b[4]; // xy = dirt,road GroundTexLayer indices; z=fog_far; w=unused
        float fog_color_near[4];  // xyz=fog colour, w=fog_near
        float blend_layers[4];    // xyz=crossfade target base,slope,cliff; w=unused
        // 112 bytes total
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

    // Fragment UBO for POM pipeline: 112 bytes + blend_layers (16 bytes) = 128 bytes
    // (exactly fills the Intel HD 520 128B push-constant budget -- do not add
    // more fields here without dropping something else first).
    struct TerrainPomFragUBO {
        float sun_dir_str[4];   // 16 bytes
        float ambient[4];       // 16 bytes
        float world_params[4];  // 16 bytes
        float pom_params[4];    // x=height_scale, y=layers_min, z=layers_max, w=fog_far
        float ground_layers_a[4]; // xyzw = base,slope,cliff,grass GroundTexLayer indices
        float ground_layers_b[4]; // xy=dirt,road GroundTexLayer indices; zw=unused
        float fog_color_near[4];  // xyz=fog colour, w=fog_near
        float blend_layers[4];    // xyz=crossfade target base,slope,cliff; w=unused
        // 128 bytes total
    };

    bool Init();
    void Shutdown();

    // Load Kenshi stitched colour overlay (md_terrain.png, 4096×4096).
    bool InitKenshiOverlay(const char* path);

    // Load 24-layer BC3 DDS texture array for per-biome ground texturing (POM path).
    // Also loads the paired 24-layer BC1 normal-map array (kGroundNmlPaths) —
    // real terrainfp4.hlsl samples diffuse+normal in lockstep per layer; see
    // biome_def.h's kGroundNmlPaths comment for the root-cause rationale.
    // Must be called after Init() and before InitPOM().
    bool InitGroundTextureArray();

    // Load the stitched grass/dirt/road painted mask (md_overlay_mask.png,
    // tools/md_stitch_overlay_mask.py — R=grass,G=secondary,B=dirt,A=road,
    // matches Kenshi's real terrainfp4.hlsl overlayMap). POM path only.
    bool InitOverlayMask(const char* path);

    // Load the procedural biome-crossfade texture (md_biome_blend.png,
    // tools/md_gen_biome_blendmap.py) — R/G/B = neighbouring-zone's
    // base/slope/cliff GroundTexLayer index (0..23, packed as raw uint8/255),
    // A = blend weight (0=pure current-zone biome, 1=pure neighbour biome).
    // Fixes the hard per-chunk (460.8m) biome edge: real Kenshi zones
    // (areasmap.tga) are numerous and small, so this transition is crossed
    // often; the real shader cross-fades it via a painted blendMap (see
    // terrainfp4.hlsl BLEND1), which we don't have hand-painted data for —
    // this derives an equivalent single extra blend slot procedurally
    // instead, delivered via texture (not UBO) since a full 4-biome-set port
    // doesn't fit the Intel HD 520 push-constant budget (TerrainPomFragUBO
    // is already 112/128 bytes). POM path only.
    bool InitBiomeBlend(const char* path);

    // Draw chunk inside an already-open GpuCommandBuffer colour pass.
    // vp16: column-major Mat4 (16 floats = 64 bytes).
    // world_origin_x/z: world coords mapped to UV centre (0.5).
    // world_to_uv: UV units per world-metre.  e.g. 1.0f/8000.0f shows 8km of Kenshi.
    void Draw(GpuCommandBuffer& cb,
              const TerrainChunk& chunk,
              const float* vp16,
              const SunParams& sun,
              float cam_x, float cam_y, float cam_z,
              float world_origin_x = 0.f,
              float world_origin_z = 0.f,
              float world_to_uv    = 1.f / 8000.f);

    // lod: 0=full 64×64, 1=32×32, 2=16×16, 3=8×8.
    // Use uniform lod for all chunks to avoid T-junctions.
    // cam_x/y/z: camera world position, for the linear distance fog applied
    // in terrain_forward.frag (matches ground.frag/NPC shader fog model).
    void DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                 const TerrainChunk& chunk,
                 const float* vp16,
                 const SunParams& sun,
                 float cam_x, float cam_y, float cam_z,
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
    // fog_far_override > 0 replaces GraphicsSettings' fog_far (tuned for normal
    // ground-level gameplay view distances). vDist is full 3D camera distance,
    // so an aerial camera (e.g. the editor's 3D World tab, kilometres up) makes
    // even chunks nearly overhead exceed the normal fog_far, saturating fog_t
    // to 1.0 and showing solid fog colour instead of the real chunk texture —
    // same root cause fixed for the batch API, see SetBatchGroundLayers's doc
    // comment. Pass 0 (default) for normal gameplay behaviour, unchanged.
    void DrawRawPOM(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                    const TerrainChunk& chunk,
                    const float* vp16,
                    const SunParams& sun,
                    float cam_x, float cam_y, float cam_z,
                    float world_origin_x = 0.f,
                    float world_origin_z = 0.f,
                    float world_to_uv    = 1.f / 8000.f,
                    int   lod            = 0,
                    float fog_far_override = 0.f);

    bool IsReady()    const;
    bool IsPomReady() const;

    // Temporarily override the kenshi colour overlay for one render batch.
    // Pass nullptr to restore the original texture.
    void UseColourOverride(SDL_GPUTexture* tex, SDL_GPUSampler* smp) {
        col_override_tex_ = tex; col_override_smp_ = smp;
    }

    // Batch API: hoists pipeline/UBO/sampler/IBO outside the per-chunk loop.
    // Reduces API calls from 6/chunk to 2/chunk for large worlds.
    // BeginRawBatch: bind pipeline, push vertex+frag UBO, bind sampler, bind shared IBO.
    // DrawRawChunk:  bind VBO + draw (2 calls per chunk).
    void BeginRawBatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                       const float* vp16, const SunParams& sun,
                       float cam_x, float cam_y, float cam_z,
                       float world_origin_x, float world_origin_z,
                       float world_to_uv, int lod);
    void DrawRawChunk(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                      const TerrainChunk& chunk);

    // For batch draws that bypass DrawRawChunk (e.g. the editor's synthesis
    // background mesh, which binds its own VBO/IBO manually): BeginRawBatch
    // never sets ground_layers_a/b — only DrawRawChunk does, per-chunk — so
    // a manual draw right after BeginRawBatch samples whatever was left over
    // from the last DrawRawChunk call (often all-zero), which showed as a
    // solid wrong-colour (green) plane. Call this once after BeginRawBatch
    // to push a real set of 6 GroundTexLayer indices before a manual draw.
    // fog_far_override > 0 replaces BeginRawBatch's fog_far (tuned for normal
    // gameplay view distances, a few km) — a whole-world background mesh
    // viewed from an aerial editor camera can be tens of km away, which
    // saturates fog_t to 1.0 under the normal fog_far and washes the entire
    // mesh out to a solid fog-colour plane (confirmed via GPU debug: ground
    // texture/UV sampling were both correct, only the final fog mix was wrong).
    // Pass 0 (default) to leave fog_far untouched.
    void SetBatchGroundLayers(SDL_GPUCommandBuffer* cmd, const float ground_layers[6],
                              float fog_far_override = 0.f);

    // Same fog_far problem as above, for the LOD1 individual-chunk path: DrawRawChunk
    // DOES set ground_layers correctly per chunk, but copies fog_far from
    // batch_fubo_base_ unchanged — so it's ALSO 100% fogged (solid fog colour)
    // whenever the aerial editor camera's altitude alone exceeds the normal
    // gameplay fog_far, regardless of horizontal distance to the chunk. Call
    // once after BeginRawBatch, before the DrawRawChunk loop, to fix.
    void SetBatchFogFar(SDL_GPUCommandBuffer* cmd, float fog_far);

private:
    GpuPipeline pipeline_;
    GpuTexture  tex_colour_;        // Kenshi colour overlay (forward pass slot 0)
    GpuTexture  tex_ground_array_;  // 24-layer BC3 DDS array (POM pass slot 0)
    GpuTexture  tex_ground_nml_array_; // 24-layer BC1 DDS array — paired normal maps
    bool        tex_loaded_        = false;
    bool        ground_array_ready_= false;

    // POM pipeline data
    GpuPipeline pom_pipeline_;
    GpuTexture  tex_detail_;   // RGB=detail tint, A=height [0,1]; tiling detail texture
    GpuTexture  tex_overlay_mask_;      // R=grass,G=secondary,B=dirt,A=road painted mask
    bool        overlay_mask_ready_ = false;
    GpuTexture  tex_biome_blend_;       // R/G/B=neighbour base/slope/cliff idx, A=blend weight
    bool        biome_blend_ready_ = false;
    PomParams   pom_params_;
    bool        pom_loaded_ = false;

    // Shared LOD IBOs — one per LOD level, built in Init(), reused by all chunks.
    GpuStaticBuffer lod_ibo_shared_[TERRAIN_LOD_LEVELS];

    // Optional colour override — UseColourOverride() swaps for one batch.
    SDL_GPUTexture* col_override_tex_ = nullptr;
    SDL_GPUSampler* col_override_smp_ = nullptr;
    uint32_t        batch_idx_count_ = 0;  // set by BeginRawBatch
    TerrainFragUBO  batch_fubo_base_{};     // sun/world params cached by BeginRawBatch;
                                             // DrawRawChunk fills ground_layers per-chunk and re-pushes

#ifdef MD_SDL_GPU
    SDL_GPUTexture* fallback_tex_            = nullptr;
    SDL_GPUSampler* fallback_sampler_        = nullptr;
    SDL_GPUTexture* fallback_detail_tex_     = nullptr;
    SDL_GPUSampler* fallback_detail_sampler_ = nullptr;
    SDL_GPUTexture* fallback_mask_tex_       = nullptr;
    SDL_GPUSampler* fallback_mask_sampler_   = nullptr;
    SDL_GPUTexture* fallback_blend_tex_      = nullptr;
    SDL_GPUSampler* fallback_blend_sampler_  = nullptr;
    void FillSamplerBindings(SDL_GPUTextureSamplerBinding out[5]) const;      // forward (LOD1-3) — POM-only pilot, not touched yet
    void FillPomSamplerBindings(SDL_GPUTextureSamplerBinding out[6]) const;  // POM (LOD0) — +b5 ground normal array
#endif
};
