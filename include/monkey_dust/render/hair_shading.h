#pragma once
// Hair shading uniform buffer — Kajiya-Kay dual-lobe specular + scatter.
// RE source: CATHODE AI.exe DAT_0164f030..007c (20 float params).
// GPU layout: set=3 binding=1 (frag UB slot 1, alongside HairFU at binding=0).
// std140: each float padded to vec4 boundary; packed as vec4 arrays.

#include <cstdio>
#include <cstring>
#include <cstdlib>

// GPU-side struct (std140, 16-byte aligned, 80 bytes total).
struct HairShadingFU {
    float light_dir[4];              // xyz = light direction, w = unused
    float eye_pos[4];                // xyz = camera/eye world pos, w = unused
    float diffuse_level;             // 0.75  — diffuse contribution
    float ambient_level;             // 0.28  — ambient floor
    float primary_spec_level;        // 0.55  — primary specular lobe strength
    float primary_spec_shininess;    // 72.0  — primary lobe sharpness
    float secondary_spec_level;      // 0.28  — secondary lobe strength
    float secondary_spec_shininess;  // 18.0  — secondary lobe (broader highlight)
    float secondary_spec_shift;      // 0.12  — secondary lobe offset along N
    float scatter_level;             // 0.18  — subsurface forward scatter
    float scatter_r;                 // 1.10  — scatter tint R (warm)
    float scatter_g;                 // 0.82  — scatter tint G
    float scatter_b;                 // 0.60  — scatter tint B
    float alpha_threshold;           // 0.12  — alpha cutoff for hair edges
};
static_assert(sizeof(HairShadingFU) == 80, "HairShadingFU std140 size mismatch");

namespace HairShading {

inline HairShadingFU g_params = {
    { 0.6f, 1.2f, 0.8f, 0.f },   // light_dir
    { 0.f,  0.f,  5.f,  0.f },   // eye_pos (updated per-frame in char_preview)
    0.75f, 0.28f,
    0.12f, 72.f,   // primary_spec_level lowered 0.55→0.12 (prevents blow-out)
    0.06f, 18.f, 0.12f,  // secondary_spec_level lowered 0.28→0.06
    0.18f, 1.10f, 0.82f, 0.60f,
    0.12f
};
inline bool g_loaded = false;

inline bool Load(const char* path = "game/data/chars/hair_shading.txt") {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[64]; float val;
        if (sscanf(line, "%63s %f", key, &val) != 2) continue;
        if (!strcmp(key, "diffuse_level"))            g_params.diffuse_level = val;
        else if (!strcmp(key, "ambient_level"))       g_params.ambient_level = val;
        else if (!strcmp(key, "primary_spec_level"))  g_params.primary_spec_level = val;
        else if (!strcmp(key, "primary_spec_shininess")) g_params.primary_spec_shininess = val;
        else if (!strcmp(key, "secondary_spec_level"))g_params.secondary_spec_level = val;
        else if (!strcmp(key, "secondary_spec_shininess")) g_params.secondary_spec_shininess = val;
        else if (!strcmp(key, "secondary_spec_shift"))g_params.secondary_spec_shift = val;
        else if (!strcmp(key, "scatter_level"))       g_params.scatter_level = val;
        else if (!strcmp(key, "scatter_color_r"))     g_params.scatter_r = val;
        else if (!strcmp(key, "scatter_color_g"))     g_params.scatter_g = val;
        else if (!strcmp(key, "scatter_color_b"))     g_params.scatter_b = val;
        else if (!strcmp(key, "alpha_threshold"))     g_params.alpha_threshold = val;
    }
    fclose(f);
    g_loaded = true;
    return true;
}

} // namespace HairShading
