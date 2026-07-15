#pragma once
#include <monkey_dust/render/render_tier.h>
#include <cstring>

// RenderQualityConfig — per-type cull radii + quality toggles.
// Inspired by Lineage 2 L2Configuration: separate TerrainCR / StaticMeshCR /
// ActorCR / StaticMeshLodCR + GL2RenderDeco + GL2TextureDetail per hardware preset.
//
// Call ApplyTierPreset(tier) once after RenderTierSystem::Detect().
// Read at render time via RenderQualityConfig::Get().

struct RenderQualityConfig {
    // ── Cull radii (metres) ─────────────────────────────────────────────────
    float terrain_cr_m    = 3000.f; // terrain chunk draw distance
    float mesh_lod0_cr_m  =  500.f; // static props LOD0 (POM close)
    float mesh_lod1_cr_m  = 1500.f; // static props LOD1
    float mesh_lod2_cr_m  = 3000.f; // static props LOD2
    float actor_cr_m      =  150.f; // NPC/character draw distance (cull.comp far_sq)
    float actor_anim_t2_m =  150.f; // NPC animation T2 (LOD2 skip skinning)

    // ── Terrain shader-pass split (hard cutoff, no crossfade) ────────────────
    // terrain_pom.frag (POM ray-march + self-shadow + normal-map array) is the
    // single most expensive fragment shader in the game and was previously run
    // unconditionally out to terrain_cr_m (task #43 fixed a POM/forward seam
    // by making POM cover the whole draw distance). Per-texel POM detail is
    // imperceptible past a few hundred metres, so restrict it to a near radius
    // and hand distant terrain to the much cheaper terrain_forward.frag. Used
    // to dithered-crossfade between the two passes around the boundary
    // (terrain_pom_band_m); that dithering caused a visible checkerboard
    // ("сітківка") and was removed (task #158, commit 4fa16546) in favour of
    // a plain hard cutoff — terrain_pom_band_m removed with it (task #158i).
    float terrain_pom_cr_m   =  150.f; // POM shader radius; beyond it → forward

    // ── Prop-type distances (metres) — replaces npc_render hardcoded consts ──
    float prop_rock_m     =  600.f;
    float prop_formation_m=  900.f;
    float prop_hat_rock_m = 1200.f;
    float prop_veg_m      =  400.f;  // yucca, canyon_rock
    float prop_tree_m     =  700.f;

    // ── Quality toggles (L2: GL2RenderDeco, GL2TextureDetail) ──────────────
    bool    render_deco   = true;   // false = skip all decorative props (vegetation)
    uint8_t tex_detail    = 0;      // 0=full mips, 1=+1 mip bias, 2=+2 mip bias
    uint8_t _pad[2]       = {};

    // ── Singleton ───────────────────────────────────────────────────────────
    static RenderQualityConfig& Get() noexcept {
        static RenderQualityConfig cfg;
        return cfg;
    }

    // Apply hardware-matched preset (call after RenderTierSystem::Detect).
    // Mirrors L2's 5-quality VRAM presets; mapped to MD's 4 RenderTier values.
    static void ApplyTierPreset(RenderTier tier) noexcept {
        RenderQualityConfig& c = Get();
        switch (tier) {
        case RenderTier::Forward:           // Intel HD 520: keep terrain LOD as-tuned
            // Terrain LOD distances unchanged from pre-RenderQualityConfig values —
            // they were already the minimum viable for HD 520 and must not shrink.
            // Savings come from prop culling + render_deco=false, NOT terrain LOD.
            c.terrain_cr_m     = 3000.f;
            c.mesh_lod0_cr_m   =  500.f;  // original: POM within 500m
            c.mesh_lod1_cr_m   = 1500.f;  // original
            c.mesh_lod2_cr_m   = 3000.f;  // original
            c.actor_cr_m       =  150.f;  // original cull UBO far_sq = 150m
            c.actor_anim_t2_m  =  150.f;
            c.terrain_pom_cr_m   = 100.f;  // HD 520: most aggressive POM cutoff
            c.prop_rock_m      =  300.f;  // reduced props = real GPU savings
            c.prop_formation_m =  450.f;
            c.prop_hat_rock_m  =  600.f;
            c.prop_veg_m       =  200.f;
            c.prop_tree_m      =  350.f;
            c.render_deco      = false;   // GL2RenderDeco=0: skip vegetation pass
            c.tex_detail       = 1;       // mip bias +1 only (was 2 = too blurry)
            break;
        case RenderTier::Deferred_Low:      // Iris 540 / Vega 8
            c.terrain_cr_m     = 3000.f;
            c.mesh_lod0_cr_m   =  500.f;
            c.mesh_lod1_cr_m   = 1500.f;
            c.mesh_lod2_cr_m   = 3000.f;
            c.actor_cr_m       =  120.f;
            c.actor_anim_t2_m  =  120.f;
            c.terrain_pom_cr_m   = 150.f;
            c.prop_rock_m      =  450.f;
            c.prop_formation_m =  650.f;
            c.prop_hat_rock_m  =  900.f;
            c.prop_veg_m       =  300.f;
            c.prop_tree_m      =  500.f;
            c.render_deco      = true;
            c.tex_detail       = 1;       // mip bias +1 → half-res
            break;
        case RenderTier::Deferred_Med:      // Vega 11
            c.terrain_cr_m     = 3000.f;
            c.mesh_lod0_cr_m   =  500.f;
            c.mesh_lod1_cr_m   = 1500.f;
            c.mesh_lod2_cr_m   = 3000.f;
            c.actor_cr_m       =  150.f;
            c.actor_anim_t2_m  =  150.f;
            c.terrain_pom_cr_m   = 200.f;
            c.prop_rock_m      =  600.f;
            c.prop_formation_m =  900.f;
            c.prop_hat_rock_m  = 1200.f;
            c.prop_veg_m       =  400.f;
            c.prop_tree_m      =  700.f;
            c.render_deco      = true;
            c.tex_detail       = 0;
            break;
        case RenderTier::Deferred_High:     // Iris Plus 640/650
            c.terrain_cr_m     = 5000.f;
            c.mesh_lod0_cr_m   =  900.f;
            c.mesh_lod1_cr_m   = 2500.f;
            c.mesh_lod2_cr_m   = 5000.f;
            c.actor_cr_m       =  200.f;
            c.actor_anim_t2_m  =  200.f;
            c.terrain_pom_cr_m   = 300.f;
            c.prop_rock_m      =  900.f;
            c.prop_formation_m = 1200.f;
            c.prop_hat_rock_m  = 1800.f;
            c.prop_veg_m       =  600.f;
            c.prop_tree_m      = 1000.f;
            c.render_deco      = true;
            c.tex_detail       = 0;
            break;
        }
    }
};
