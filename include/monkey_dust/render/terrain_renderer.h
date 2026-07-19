#pragma once
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/ssbo.h>
#include <monkey_dust/world/terrain_chunk.h>

// TerrainRenderer — the terrain rendering pass for a TerrainChunk. Uses
// terrain_forward.vert/frag: single colour output, Lambertian lighting,
// per-vertex dominant-weight ground selection (see TerrainVertex's
// ground_id/ground_id2/blend_alpha, terrain_chunk.h). One shader for every
// distance — real Kenshi has no near/far terrain-shader split either
// (re_docs/kenshi/terrain.md:114-136). 2026-07-19: removed the POM
// (parallax occlusion mapping + bump-normal) pipeline and the per-chunk
// baked-albedo pipeline that used to exist alongside this one — both were
// this project's own invention, not part of the original game, and were
// the root of a whole class of near/far shader-seam bugs. For GBuffer
// deferred path see terrain.vert/frag (not yet wired).
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

    // Vertex UBO: mat4 vp (64) + world params (16) + cam_pos_ws (16) = 96 bytes.
    struct TerrainVertUBO {
        float vp[16];           // column-major 4×4 MVP
        float world_origin_x;   // world X → UV 0.5
        float world_origin_z;   // world Z → UV 0.5
        float world_to_uv;      // UV per metre (1 / kenshi_view_metres)
        // Mesh-LOD dithered crossfade (task #43) — every caller that set
        // this to nonzero was removed by task #158 (2026-07-15); kept in
        // the layout for source/UBO compatibility, effectively always 0.
        float lod_blend;
        float cam_pos_ws[4];    // xyz=camera world position, w=unused (fog distance)
        // No longer consumed by the shader (was the per-chunk albedo-bake
        // UV origin, terrain_albedo_bake.slang — deleted 2026-07-19, see
        // TerrainVertex's ground_id/ground_id2/blend_alpha in
        // terrain_chunk.h). Kept so every DrawRaw/DrawRawChunk caller's
        // argument list stays unchanged — touching that is out of scope
        // for this rewrite.
        float chunk_origin_x;
        float chunk_origin_z;
        // Applied to aPos.xz BEFORE everything else (projection, vWorldPos,
        // vDist) — lets a single static, absolute-Kenshi-metres vertex
        // buffer (the game's whole-world compact-VBO background, task
        // #158i-9/9) render correctly aligned with the game's floating
        // local terrain window (tnoff_x/z) without rebuilding the buffer
        // every time that window re-centres. Zero for every other caller
        // (BeginRawBatch defaults it to 0,0) — no behaviour change there.
        float pos_offset_x;
        float pos_offset_z;
        // 112 bytes total
    };

    // Fragment UBO: sun(32) + world_params(16) + ground_layers_a/b(32) + fog_color_near(16)
    // + blend_layers(16) + use_zone_lookup(16) = 128 bytes.
    struct TerrainFragUBO {
        float sun_dir_str[4];     // xyz=dir, w=strength
        float ambient[4];         // xyz=colour, w=unused
        float world_params[4];    // xy=origin_xz, z=world_to_uv, w=unused
        float ground_layers_a[4]; // xyzw = base,slope,cliff,grass GroundTexLayer indices
        float ground_layers_b[4]; // xy = dirt,road GroundTexLayer indices; z=fog_far (linear fog, 2026-07-12 — was fog_density/FOG_EXP2 briefly, reverted: EXP2 saturated fog_t≈1.0 across most of a normal game view); w=unused
        float fog_color_near[4];  // xyz=fog colour, w=fog_near
        float blend_layers[4];    // xyz=crossfade target base,slope,cliff; w=SLOT-B base index (used per-chunk, see terrain_gen.cpp — NOT a free/unused slot)
        // x=1.0 -> override ground_layers_a/b via the per-zone SSBO lookup
        // (see UploadZoneGroundLayers/SetBatchZoneLookup); 0.0 = normal
        // per-chunk/per-batch ground_layers_a/b (unchanged existing path).
        // yzw unused/reserved. MUST be explicitly zeroed by BeginRawBatch —
        // batch_fubo_base_ is a persistent member reused across draws, and
        // DrawRawChunk copies it verbatim without touching this field, so a
        // stale 1.0 from an earlier synthesis/compact-LOD2 draw this same
        // frame would otherwise silently leak into subsequent per-chunk draws.
        float use_zone_lookup[4];
        // 128 bytes total — exactly the Intel HD 520 frag UBO ceiling.
    };

    bool Init();
    void Shutdown();

    // Load Kenshi stitched colour overlay (md_terrain.png, 4096×4096).
    bool InitKenshiOverlay(const char* path);

    // Load 24-layer BC3 DDS texture array for per-biome ground texturing.
    // Also loads the paired 24-layer BC1 normal-map array (kGroundNmlPaths)
    // — currently unused by terrain_forward.slang (no normal-mapping since
    // the 2026-07-19 rewrite) but kept loaded; see biome_def.h's
    // kGroundNmlPaths comment. Must be called after Init().
    bool InitGroundTextureArray();

    // Load the tiling generic-detail RGB tint texture used by
    // terrain_forward.slang's albedo blend (was loaded as a side effect of
    // InitPOM's detail_path parameter before the 2026-07-19 POM removal —
    // now its own small entry point). Pass nullptr/empty to leave
    // tex_detail_ unset (FillSamplerBindings falls back to a neutral white
    // 1×1, same as any other missing texture).
    bool InitDetailTexture(const char* path);

    // Load the stitched grass/dirt/road painted mask (md_overlay_mask.png,
    // tools/md_stitch_overlay_mask.py — R=grass,G=secondary,B=dirt,A=road,
    // matches Kenshi's real terrainfp4.hlsl overlayMap). Used by the
    // editor's zone-lookup (synthesis/compact-LOD) draw path only — the
    // per-chunk near/mid path resolves this at generation time instead
    // (see terrain_gen.cpp's s_ground_pick).
    bool InitOverlayMask(const char* path);

    // Load the procedural biome-crossfade texture (md_biome_blend.png,
    // tools/md_gen_biome_blendmap.py) — R/G/B = neighbouring-zone's
    // base/slope/cliff GroundTexLayer index (0..23, packed as raw uint8/255),
    // A = blend weight (0=pure current-zone biome, 1=pure neighbour biome).
    // Same zone-lookup-path-only scope as InitOverlayMask above — the
    // per-chunk near/mid path no longer needs this (per-vertex ground
    // selection resolves zone/chunk-boundary blending directly).
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
    // lod_blend: geomorph factor (task #182g, 2026-07-19 — repurposed from
    // an unused dithered-discard crossfade, see terrain_forward.slang's
    // vsMain/aMorphY doc comments) — 0=full LOD0 detail (default), ramping
    // to 1 as this chunk approaches its LOD0->LOD1 distance threshold
    // blends vertex Y toward the pre-baked coarser-LOD target position,
    // hiding the hard "pop" at the actual LOD switch. Caller (game/src/
    // render/npc_render.cpp) computes this from distance; 0 elsewhere
    // (editor Draw()/DrawRawChunk() don't wire it up yet).
    // fog_density_override > 0 replaces the terrain_cr_m-derived fog_far
    // (linear fog, see graphics_settings.h's fog_near_ratio comment). Normal
    // gameplay fog_far is tuned for ground-level view distance; an aerial
    // camera (e.g. the editor's 3D World tab, kilometres up) needs a much
    // LARGER fog_far or distant chunks saturate to solid fog colour. Pass 0
    // (default) to leave fog_far untouched.
    void DrawRaw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                 const TerrainChunk& chunk,
                 const float* vp16,
                 const SunParams& sun,
                 float cam_x, float cam_y, float cam_z,
                 float world_origin_x = 0.f,
                 float world_origin_z = 0.f,
                 float world_to_uv    = 1.f / 8000.f,
                 int   lod            = 0,
                 float lod_blend      = 0.f,
                 float fog_density_override = 0.f);

    bool IsReady() const;

    // Batch API: hoists pipeline/UBO/sampler/IBO outside the per-chunk loop.
    // Reduces API calls from 6/chunk to 2/chunk for large worlds.
    // BeginRawBatch: bind pipeline, push vertex+frag UBO, bind sampler, bind shared IBO.
    // DrawRawChunk:  bind VBO + draw (2 calls per chunk).
    // pos_offset_x/z: see TerrainVertUBO::pos_offset_x's doc comment — 0,0 for
    // every caller except the game's whole-world compact-VBO background.
    void BeginRawBatch(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                       const float* vp16, const SunParams& sun,
                       float cam_x, float cam_y, float cam_z,
                       float world_origin_x, float world_origin_z,
                       float world_to_uv, int lod,
                       float pos_offset_x = 0.f, float pos_offset_z = 0.f);
    void DrawRawChunk(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd,
                      const TerrainChunk& chunk);

    // For batch draws that bypass DrawRawChunk (e.g. the editor's synthesis
    // background mesh, which binds its own VBO/IBO manually): BeginRawBatch
    // never sets ground_layers_a/b — only DrawRawChunk does, per-chunk — so
    // a manual draw right after BeginRawBatch samples whatever was left over
    // from the last DrawRawChunk call (often all-zero), which showed as a
    // solid wrong-colour (green) plane. Call this once after BeginRawBatch
    // to push a real set of 6 GroundTexLayer indices before a manual draw.
    // fog_density_override > 0 replaces BeginRawBatch's fog_far (linear fog,
    // 2026-07-12 — name kept for source compatibility, was an EXP2 density
    // override). Default fog_far is terrain_cr_m-derived, tuned for normal
    // gameplay view distances (a few km) — a whole-world background mesh
    // viewed from an aerial editor camera can be tens of km away, which
    // would saturate fog_t to 1.0 and wash the entire mesh to solid fog
    // colour; pass a LARGER fog_far override for that case. Pass 0 (default)
    // to leave fog_far untouched.
    void SetBatchGroundLayers(SDL_GPUCommandBuffer* cmd, const float ground_layers[6],
                              float fog_density_override = 0.f);

    // Same fog problem as above, for the LOD1 individual-chunk path: DrawRawChunk
    // DOES set ground_layers correctly per chunk, but copies fog_far from
    // batch_fubo_base_ unchanged — so it's ALSO 100% fogged (solid fog colour)
    // whenever an aerial camera's distance to the whole-mesh draw exceeds
    // fog_far, regardless of horizontal distance to any one chunk.
    // Call once after BeginRawBatch, before the DrawRawChunk loop, to fix.
    void SetBatchFogDensity(SDL_GPUCommandBuffer* cmd, float fog_density);

    // Upload the per-zone (64x64=4096) ground-layer lookup table: 6 uint32
    // per zone (base,slope,cliff,grass,dirt,road GroundTexLayer indices),
    // flat layout index = zone_idx*6 + layer, zone_idx = zy*64+zx (matches
    // the EDITOR_TNKN=64 bitmask convention used elsewhere). Built once by
    // the caller (World3D editor's synthesis-mesh init) via
    // TerrainGen_ResolveBiome() per zone — same biome resolution the
    // correctly-working per-chunk LOD0/1 path already uses. count_uints
    // must be exactly 64*64*6 = 24576.
    void UploadZoneGroundLayers(const uint32_t* data, int count_uints);

    // Toggle the per-fragment zone-lookup ground-texture path for the
    // current batch (packed into TerrainFragUBO.blend_layers.w, currently
    // unused — zero struct-size change) instead of the fixed
    // ground_layers_a/b pushed by SetBatchGroundLayers. Used by the
    // editor's synthesis/compact-LOD2 background draws, which have no
    // single "current biome" for their whole (multi-zone) draw call — see
    // SetBatchGroundLayers's doc comment for the bug this replaces.
    // Biome crossfade (blend_layers.xyz/tex_biome_blend) is not applied on
    // this path — hard per-zone boundary, documented simplification (see
    // terrain_forward.slang).
    void SetBatchZoneLookup(SDL_GPUCommandBuffer* cmd, bool enable, float fog_density_override = 0.f);

private:
    GpuPipeline pipeline_;
    GpuTexture  tex_colour_;        // Kenshi colour overlay
    GpuTexture  tex_ground_array_;  // 24-layer BC3 DDS array — the actual per-vertex-indexed ground textures
    GpuTexture  tex_ground_nml_array_; // 24-layer BC1 DDS array — paired normal maps, currently unused (no normal-mapping)
    bool        tex_loaded_        = false;
    bool        ground_array_ready_= false;

    GpuTexture  tex_detail_;   // generic tiling detail-tint texture (InitDetailTexture)
    GpuTexture  tex_overlay_mask_;      // R=grass,G=secondary,B=dirt,A=road painted mask — zone-lookup path only
    bool        overlay_mask_ready_ = false;
    GpuTexture  tex_biome_blend_;       // R/G/B=neighbour base/slope/cliff idx, A=blend weight — zone-lookup path only
    bool        biome_blend_ready_ = false;

    // Shared LOD IBOs — one per LOD level, built in Init(), reused by all chunks.
    GpuStaticBuffer lod_ibo_shared_[TERRAIN_LOD_LEVELS];

    // Per-zone ground-layer lookup (see UploadZoneGroundLayers/SetBatchZoneLookup).
    SSBO zone_layers_ssbo_;

    // Task #182 (2026-07-19): all-flat (steepness=0) fallback for
    // TerrainChunk::steepness_ssbo, bound whenever a chunk hasn't baked one
    // yet (e.g. mid-stream) or on the zone-lookup batch path (BeginRawBatch
    // has no single "current chunk" — same reasoning as chunk_origin_x/z=0,0
    // there). Never actually read by fsMain's use_lookup branch, but the
    // pipeline declares 2 fragment storage buffers so both slots must be
    // bound with SOMETHING valid every draw, or the second binding reads
    // garbage (Intel HD 520 checklist: sampler/buffer count mismatches
    // silently corrupt neighbouring bindings).
    GpuStaticBuffer fallback_steepness_ssbo_;

    // Task #182c/d (2026-07-19): the real Kenshi biome-crossfade alternates
    // are a small GLOBAL (world-wide-constant, not per-chunk/per-page) set —
    // confirmed via Ghidra decompile of the render-time lookup chain plus
    // real FCS "Biomes" data (see terrain_gen.cpp's ARCHITECTURE NOTE and
    // re_docs/kenshi/terrain.md). Resolved lazily on first FillStorageBindings
    // call (BiomeRegistry must already be loaded by then — chunk generation,
    // which depends on it too, always runs before the first draw) via
    // BiomeRegistry::ForColor(255,0,0)/(0,255,0)/(0,0,255), then uploaded
    // ONCE as 3×6=18 floats (ground/slope/cliff/grass/dirt/road GroundTexLayer
    // indices per alternate) — never changes afterward, same instance bound
    // on every draw call regardless of which chunk.
    GpuStaticBuffer combo_alt_ssbo_;
    bool            combo_alt_ready_ = false;

    uint32_t        batch_idx_count_ = 0;  // set by BeginRawBatch
    TerrainFragUBO  batch_fubo_base_{};     // sun/world params cached by BeginRawBatch;
                                             // DrawRawChunk fills ground_layers per-chunk and re-pushes

#ifdef MD_SDL_GPU
    SDL_GPUTexture* fallback_tex_            = nullptr;
    SDL_GPUSampler* fallback_sampler_        = nullptr;
    SDL_GPUTexture* fallback_mask_tex_       = nullptr;
    SDL_GPUSampler* fallback_mask_sampler_   = nullptr;
    SDL_GPUTexture* fallback_blend_tex_      = nullptr;
    SDL_GPUSampler* fallback_blend_sampler_  = nullptr;
    void FillSamplerBindings(SDL_GPUTextureSamplerBinding out[5]) const;
    // Task #182: binding 0 = zoneGroundLayers (global), binding 1 = the
    // chunk's own steepness_ssbo (or fallback_steepness_ssbo_ if unbaked),
    // binding 2 = comboAlternates (global, see combo_alt_ssbo_'s doc comment;
    // lazily resolved+uploaded on first call — not const-safe on paper, but
    // matches the existing lazy-init pattern used elsewhere in this class).
    void FillStorageBindings(SDL_GPUBuffer* out[3], const TerrainChunk* chunk);
#endif
};
