#pragma once
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/render/ssbo.h>
#include <monkey_dust/world/terrain_chunk.h>

// TerrainRenderer — shared ground-texturing RESOURCE MANAGER for
// TerrainPatchRenderer/Granite (task terrain-dedup, 2026-07-29). Despite
// the name, this class no longer draws anything itself: its own draw
// pipeline (terrain_forward.vert/frag) and Draw/DrawRaw/BeginRawBatch/
// DrawRawChunk/SetBatch* methods were verified this session to have ZERO
// callers left anywhere in game/ or tools/editor/ (grepped both) — dead
// weight from before the Granite migration (Phase 8) replaced ALL visual
// terrain rendering. What's still genuinely alive and used by Granite's
// TerrainPatchRenderer::DrawBatch (via GetSharedGroundSamplers/
// ZoneGroundLayersSSBO): loading + owning the shared ground textures
// (Kenshi colour overlay, per-biome ground DDS array, offline-baked
// flat-ground colour) and the per-zone ground-layer lookup SSBO, so
// Granite doesn't need a second ~1GB+ copy of the same data. Kept the
// class name as-is (renaming would touch every call site across game/
// tools/editor -- out of scope for this dead-code removal pass).
//
// NOT addressed here (separate, larger, riskier question — flagged, not
// investigated this pass): whether TerrainChunk's own vbo/ibo/skirt
// mesh buffers (built by terrain_gen.cpp/terrain_upload.cpp) are ALSO
// now dead now that nothing draws them directly, or whether physics/
// collision/the editor's terrain-sculpt panel still depend on them.
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

    // Load the offline-baked flat-ground colour (md_ground_baked.dds,
    // tools/md_bake_ground_layers.py + tools/md_encode_ground_bake.py) --
    // pre-resolved base/slope/grass/dirt/road blend (matches BlendGroundLayers'
    // sequence exactly, minus the final cliff mix) for TerrainPatchRenderer's
    // single-sample flat-ground path. Cliff stays live/triplanar (task #306,
    // see /home/rdga1/.claude/plans/serene-pondering-teapot.md).
    bool InitGroundBaked(const char* path);

    // Load the procedural biome-crossfade texture (md_biome_blend.png,
    // tools/md_gen_biome_blendmap.py) — R/G/B = neighbouring-zone's
    // base/slope/cliff GroundTexLayer index (0..23, packed as raw uint8/255),
    // A = blend weight (0=pure current-zone biome, 1=pure neighbour biome).
    // Same zone-lookup-path-only scope as InitGroundBaked above — the
    // per-chunk near/mid path no longer needs this (per-vertex ground
    // selection resolves zone/chunk-boundary blending directly).
    bool InitBiomeBlend(const char* path);

    // Load the Kenshi grass/dirt/road paint mask (md_overlay_mask.png,
    // tools/md_stitch_overlay_mask.py — R=grass, G=grass2, B=dirt, A=road,
    // same UV space as tex_colour/InitKenshiOverlay). Was only consumed
    // offline (tools/md_bake_ground_layers.py) until task
    // terrain-detail-erases-road (2026-08-01): TerrainPatchRenderer's
    // close-range detail-restore layer needs this live too, so its fine
    // detail sample can favour grass/dirt/road over base ground the same
    // way the far-range bake already does — without it, close-range detail
    // silently overwrote baked-in roads with pure base texture.
    bool InitOverlayMask(const char* path);

    bool IsReady() const;

    // Exposes 4 of the already-loaded ground-shading textures (colour
    // overlay, per-biome ground DDS array, offline-baked flat-ground colour,
    // grass/dirt/road paint mask, in that order) — TerrainPatchRenderer/
    // Granite's DrawBatch is the sole consumer now, avoiding a second,
    // wasteful ~1GB+ reload of the same data. tex_biome_blend deliberately
    // excluded: no caller of this needs it.
    void GetSharedGroundSamplers(SDL_GPUTextureSamplerBinding out[4]) const;
    // Per-zone (64x64=4096) ground-layer lookup SSBO — same data
    // UploadZoneGroundLayers populates, exposed for the same reuse reason.
    SDL_GPUBuffer* ZoneGroundLayersSSBO() const { return zone_layers_ssbo_.SDLBuffer(); }

    // Upload the per-zone (64x64=4096) ground-layer lookup table: 8 uint32
    // per zone -- [0..5] base,slope,cliff,grass,dirt,road GroundTexLayer
    // indices, [6..7] real per-biome cliff UV tiling scale (bit-cast float,
    // BiomeDef::cliff_tiling_x/y) -- flat layout index = zone_idx*8 + slot,
    // zone_idx = zy*64+zx (matches the EDITOR_TNKN=64 bitmask convention
    // used elsewhere). Built once by the caller (World3D editor's
    // synthesis-mesh init) via TerrainGen_ResolveBiome() per zone — same
    // biome resolution the correctly-working per-chunk LOD0/1 path already
    // uses. count_uints must be exactly 64*64*8 = 32768.
    void UploadZoneGroundLayers(const uint32_t* data, int count_uints);

private:
    GpuTexture  tex_colour_;        // Kenshi colour overlay
    GpuTexture  tex_ground_array_;  // 24-layer BC3 DDS array — the actual per-vertex-indexed ground textures
    GpuTexture  tex_ground_nml_array_; // 24-layer BC1 DDS array — paired normal maps, currently unused (no normal-mapping)
    bool        tex_loaded_        = false;
    bool        ground_array_ready_= false;

    GpuTexture  tex_ground_baked_;      // offline-baked flat-ground colour (task #306) — TerrainPatchRenderer's flat-ground sample
    bool        ground_baked_ready_ = false;
    GpuTexture  tex_biome_blend_;       // R/G/B=neighbour base/slope/cliff idx, A=blend weight — loaded, currently unconsumed (see InitBiomeBlend)
    bool        biome_blend_ready_ = false;
    GpuTexture  tex_overlay_mask_;      // R=grass, G=grass2, B=dirt, A=road (see InitOverlayMask)
    bool        overlay_mask_ready_ = false;

    // Per-zone ground-layer lookup (see UploadZoneGroundLayers).
    SSBO zone_layers_ssbo_;

#ifdef MD_SDL_GPU
    SDL_GPUTexture* fallback_tex_            = nullptr;
    SDL_GPUSampler* fallback_sampler_        = nullptr;
    SDL_GPUTexture* fallback_mask_tex_       = nullptr;
    SDL_GPUSampler* fallback_mask_sampler_   = nullptr;
    SDL_GPUTexture* fallback_blend_tex_      = nullptr;
    SDL_GPUSampler* fallback_blend_sampler_  = nullptr;
    void FillSamplerBindings(SDL_GPUTextureSamplerBinding out[5]) const;
#endif
};
