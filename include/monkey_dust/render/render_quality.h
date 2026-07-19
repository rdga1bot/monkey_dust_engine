#pragma once
#include <monkey_dust/render/render_tier.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstdio>
#include <cstdlib>
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
    // mesh_lod0/1/2_cr_m: DOUBLE DUTY (task #182g audit, 2026-07-19 — not
    // previously documented) — originally "static props LOD" per the names
    // below, but game/src/render/npc_render.cpp's terrain draw loop ALSO
    // reuses these exact 3 fields as the GAME's terrain chunk mesh-LOD
    // distance thresholds (SceneRender's fixed 9x9 window). The EDITOR's
    // World3D 64x64 aerial viewport does NOT use these — it has its own
    // separate, deliberately larger hardcoded d0sq/d1sq/d2sq (1200/3500/
    // 8000) in tools/editor/editor_world_3d_sdlgpu.cpp. See also
    // engine/include/monkey_dust/world/terrain_chunk.h's removed
    // TERRAIN_LOD_DIST comment for the full 2-systems-not-3 picture.
    float mesh_lod0_cr_m  =  500.f; // static props LOD0 (POM close) + game terrain LOD0
    float mesh_lod1_cr_m  = 1500.f; // static props LOD1 + game terrain LOD1
    float mesh_lod2_cr_m  = 3000.f; // static props LOD2 + game terrain LOD2
    float actor_cr_m      =  150.f; // NPC/character draw distance (cull.comp far_sq)
    float actor_anim_t2_m =  150.f; // NPC animation T2 (LOD2 skip skinning)

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

    // Optional runtime tuning without a C++ recompile (user request,
    // 2026-07-18 terrain-seam investigation — every numeric experiment that
    // session needed a full ninja rebuild). Call once, right after
    // ApplyTierPreset, so overrides win over the tier default. Plain
    // key=value lines (one per line, '#' starts a comment, blank lines
    // ignored) — no external JSON library per project convention. Missing
    // file is silent/expected (most runs have no overrides).
    static void LoadOverrides(const char* path = "game/data/render_quality_overrides.txt") noexcept {
        uint32_t size = 0;
        char* buf = md::fs_read_alloc(path, &size);
        if (!buf) return;
        RenderQualityConfig& c = Get();
        char* line = buf;
        while (line < buf + size) {
            char* nl = (char*)memchr(line, '\n', (buf + size) - line);
            char* end = nl ? nl : (buf + size);
            *end = '\0';
            char* eq = strchr(line, '=');
            if (eq && line[0] != '#') {
                *eq = '\0';
                const char* key = line;
                const char* val = eq + 1;
                float fval = (float)atof(val);
                if      (!strcmp(key, "terrain_cr_m"))    c.terrain_cr_m    = fval;
                else if (!strcmp(key, "mesh_lod0_cr_m"))  c.mesh_lod0_cr_m  = fval;
                else if (!strcmp(key, "mesh_lod1_cr_m"))  c.mesh_lod1_cr_m  = fval;
                else if (!strcmp(key, "mesh_lod2_cr_m"))  c.mesh_lod2_cr_m  = fval;
                else if (!strcmp(key, "actor_cr_m"))      c.actor_cr_m      = fval;
                else if (!strcmp(key, "actor_anim_t2_m")) c.actor_anim_t2_m = fval;
                else if (!strcmp(key, "prop_rock_m"))     c.prop_rock_m     = fval;
                else if (!strcmp(key, "prop_formation_m"))c.prop_formation_m= fval;
                else if (!strcmp(key, "prop_hat_rock_m")) c.prop_hat_rock_m = fval;
                else if (!strcmp(key, "prop_veg_m"))      c.prop_veg_m      = fval;
                else if (!strcmp(key, "prop_tree_m"))     c.prop_tree_m     = fval;
                else if (!strcmp(key, "render_deco"))     c.render_deco     = fval != 0.f;
                else if (!strcmp(key, "tex_detail"))       c.tex_detail      = (uint8_t)fval;
                else    fprintf(stderr, "[RenderQualityConfig] unknown override key: %s\n", key);
            }
            line = end + 1;
        }
        md::fs_free(buf);
        fprintf(stdout, "[RenderQualityConfig] loaded overrides from %s\n", path);
    }
};
