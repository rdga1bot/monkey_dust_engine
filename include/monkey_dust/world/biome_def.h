#pragma once
#include <cstring>
// Biome definitions — per-zone terrain textures (real Kenshi DDS converted to JPG),
// fog colour, and sky horizon colour.

#define T(n) "game/data/textures/terrain/" n

struct BiomeDef {
    const char* tex0;
    const char* tex1;
    const char* tex2;
    const char* tex3;
    float fog_r, fog_g, fog_b;
    float sky_horizon_r, sky_horizon_g, sky_horizon_b;

    static const BiomeDef& ForZone(const char* zone_slug);
};

static const struct { const char* slug; BiomeDef def; } kBiomeTable[] = {
    // ── DESERT ──────────────────────────────────────────────────────────────
    // Warm yellow-tan sand; almost no vegetation in Kenshi
    {"great_desert",    {T("k_desert_sand.jpg"),  T("k_desert_rock.jpg"),
                         T("k_desert_slabs.jpg"), T("k_desert_sandstone.jpg"),
                         0.72f,0.60f,0.35f,  0.90f,0.72f,0.40f}},
    {"stenn_desert",    {T("k_desert_sand.jpg"),  T("k_desert_sandstone.jpg"),
                         T("k_desert_rock.jpg"),  T("k_desert_slabs.jpg"),
                         0.75f,0.62f,0.34f,  0.92f,0.75f,0.42f}},
    {"skimsands",       {T("k_desert_sand.jpg"),  T("k_desert_slabs.jpg"),
                         T("k_desert_sandstone.jpg"), T("k_desert_rock.jpg"),
                         0.78f,0.65f,0.38f,  0.94f,0.78f,0.46f}},
    {"bonefields",      {T("k_high_bone.jpg"),    T("k_desert_sand.jpg"),
                         T("k_high_gravel.jpg"),  T("k_desert_rock.jpg"),
                         0.70f,0.65f,0.48f,  0.85f,0.75f,0.55f}},
    {"high_bonefields", {T("k_high_bone.jpg"),    T("k_high_gravel.jpg"),
                         T("k_desert_sand.jpg"),  T("k_high_rock.jpg"),
                         0.68f,0.62f,0.50f,  0.82f,0.72f,0.56f}},
    {"stobe_gamble",    {T("k_desert_rock.jpg"),  T("k_canyon_cliff.jpg"),
                         T("k_desert_sandstone.jpg"), T("k_high_gravel.jpg"),
                         0.62f,0.55f,0.38f,  0.78f,0.65f,0.44f}},

    // ── VOLCANIC / ASHLANDS ─────────────────────────────────────────────────
    // Dark grey ash — one of Kenshi's most distinctive zones
    {"ashlands",        {T("k_volc_ash.jpg"),     T("k_volc_dark.jpg"),
                         T("k_volc_floor.jpg"),   T("k_volc_hell.jpg"),
                         0.18f,0.14f,0.12f,  0.30f,0.18f,0.10f}},
    {"venge",           {T("k_volc_dark.jpg"),    T("k_volc_hell.jpg"),
                         T("k_volc_ash.jpg"),     T("k_volc_floor.jpg"),
                         0.15f,0.10f,0.08f,  0.22f,0.12f,0.08f}},
    {"deadlands",       {T("k_volc_ash.jpg"),     T("k_volc_floor.jpg"),
                         T("k_high_bone.jpg"),    T("k_high_gravel.jpg"),
                         0.22f,0.18f,0.16f,  0.34f,0.24f,0.18f}},

    // ── CANYON / ROCKY ──────────────────────────────────────────────────────
    // Red-orange sandstone canyon walls — rocky_canyon, dreg, etc.
    {"rocky_canyon",    {T("k_canyon_plain.jpg"), T("k_canyon_cliff.jpg"),
                         T("k_canyon_sand.jpg"),  T("k_canyon_layer.jpg"),
                         0.42f,0.32f,0.22f,  0.62f,0.48f,0.28f}},
    {"dreg",            {T("k_canyon_cliff.jpg"), T("k_canyon_layer.jpg"),
                         T("k_scrub_rock.jpg"),   T("k_high_rock.jpg"),
                         0.40f,0.34f,0.26f,  0.58f,0.48f,0.32f}},
    {"the_pits",        {T("k_canyon_cliff.jpg"), T("k_volc_dark.jpg"),
                         T("k_canyon_sand.jpg"),  T("k_canyon_layer.jpg"),
                         0.28f,0.24f,0.20f,  0.42f,0.34f,0.24f}},
    {"stobe_garden",    {T("k_canyon_sand.jpg"),  T("k_canyon_plain.jpg"),
                         T("k_desert_sandstone.jpg"), T("k_canyon_layer.jpg"),
                         0.50f,0.42f,0.28f,  0.68f,0.56f,0.34f}},
    {"okran_pride",     {T("k_scrub_flat.jpg"),   T("k_canyon_plain.jpg"),
                         T("k_scrub_dirt.jpg"),   T("k_desert_sandstone.jpg"),
                         0.55f,0.50f,0.35f,  0.70f,0.62f,0.42f}},

    // ── SWAMP ───────────────────────────────────────────────────────────────
    // Green-grey murky terrain — Shem, Gut, Swamplands
    {"swamplands",      {T("k_swamp_mud.jpg"),    T("k_swamp_rock.jpg"),
                         T("k_swamp_floor.jpg"),  T("k_swamp_dark.jpg"),
                         0.22f,0.30f,0.20f,  0.30f,0.42f,0.26f}},
    {"shem",            {T("k_swamp_mud.jpg"),    T("k_swamp_floor.jpg"),
                         T("k_swamp_rock.jpg"),   T("k_swamp_dark.jpg"),
                         0.20f,0.28f,0.18f,  0.28f,0.40f,0.24f}},
    {"gut",             {T("k_swamp_dark.jpg"),   T("k_swamp_mud.jpg"),
                         T("k_swamp_rock.jpg"),   T("k_swamp_floor.jpg"),
                         0.16f,0.24f,0.16f,  0.22f,0.34f,0.20f}},
    {"greenbeach",      {T("k_swamp_floor.jpg"),  T("k_swamp_mud.jpg"),
                         T("k_scrub_flat.jpg"),   T("k_scrub_rock.jpg"),
                         0.28f,0.38f,0.26f,  0.38f,0.50f,0.32f}},
    {"floodlands",      {T("k_swamp_mud.jpg"),    T("k_swamp_dark.jpg"),
                         T("k_scrub_mud.jpg"),    T("k_swamp_rock.jpg"),
                         0.24f,0.32f,0.22f,  0.32f,0.44f,0.28f}},
    {"sonorous_dark",   {T("k_swamp_dark.jpg"),   T("k_high_rock.jpg"),
                         T("k_scrub_rock.jpg"),   T("k_volc_ash.jpg"),
                         0.20f,0.22f,0.18f,  0.28f,0.30f,0.24f}},

    // ── SCRUBLAND ───────────────────────────────────────────────────────────
    // Brown-grey dry plains — Border Zone, Hub, Cannibal Plains
    {"border_zone",     {T("k_scrub_dirt.jpg"),   T("k_scrub_rock.jpg"),
                         T("k_scrub_flat.jpg"),   T("k_scrub_mud.jpg"),
                         0.48f,0.42f,0.28f,  0.62f,0.54f,0.36f}},
    {"cannibal_plains", {T("k_scrub_flat.jpg"),   T("k_scrub_dirt.jpg"),
                         T("k_desert_rock.jpg"),  T("k_scrub_rock.jpg"),
                         0.50f,0.44f,0.26f,  0.65f,0.56f,0.34f}},
    {"skinner_roam",    {T("k_scrub_mud.jpg"),    T("k_scrub_flat.jpg"),
                         T("k_scrub_rock.jpg"),   T("k_scrub_dirt.jpg"),
                         0.46f,0.40f,0.26f,  0.60f,0.52f,0.34f}},
    {"the_hub",         {T("k_scrub_dirt.jpg"),   T("k_scrub_flat.jpg"),
                         T("k_scrub_rock.jpg"),   T("k_desert_sandstone.jpg"),
                         0.52f,0.46f,0.30f,  0.66f,0.58f,0.38f}},
    {"iron_valleys",    {T("k_scrub_rock.jpg"),   T("k_high_rock.jpg"),
                         T("k_scrub_dirt.jpg"),   T("k_high_earth.jpg"),
                         0.40f,0.36f,0.28f,  0.52f,0.46f,0.34f}},
    {"howler_maze",     {T("k_high_rock.jpg"),    T("k_scrub_rock.jpg"),
                         T("k_high_earth.jpg"),   T("k_volc_ash.jpg"),
                         0.32f,0.30f,0.26f,  0.44f,0.40f,0.34f}},

    // ── HIGHLANDS ───────────────────────────────────────────────────────────
    // Cool grey-brown rocky ridges — Shek Kingdom, Fog Islands
    {"shek_territory",  {T("k_high_earth.jpg"),   T("k_high_rock.jpg"),
                         T("k_scrub_rock.jpg"),   T("k_high_gravel.jpg"),
                         0.38f,0.36f,0.32f,  0.50f,0.48f,0.42f}},
    {"the_crags",       {T("k_high_rock.jpg"),    T("k_high_earth.jpg"),
                         T("k_canyon_cliff.jpg"), T("k_high_gravel.jpg"),
                         0.35f,0.32f,0.28f,  0.46f,0.42f,0.36f}},
    {"bast",            {T("k_high_earth.jpg"),   T("k_scrub_rock.jpg"),
                         T("k_high_rock.jpg"),    T("k_high_gravel.jpg"),
                         0.40f,0.38f,0.32f,  0.52f,0.50f,0.42f}},
    {"the_spine",       {T("k_high_rock.jpg"),    T("k_high_earth.jpg"),
                         T("k_volc_ash.jpg"),     T("k_high_gravel.jpg"),
                         0.30f,0.28f,0.26f,  0.40f,0.38f,0.34f}},
    {"fog_islands",     {T("k_high_earth.jpg"),   T("k_swamp_rock.jpg"),
                         T("k_high_rock.jpg"),    T("k_scrub_flat.jpg"),
                         0.35f,0.40f,0.38f,  0.45f,0.52f,0.48f}},
    {"leviathan_coast", {T("k_high_rock.jpg"),    T("k_high_gravel.jpg"),
                         T("k_swamp_rock.jpg"),   T("k_high_earth.jpg"),
                         0.38f,0.44f,0.42f,  0.48f,0.56f,0.52f}},
    {"holy_nation",     {T("k_scrub_flat.jpg"),   T("k_desert_sandstone.jpg"),
                         T("k_scrub_dirt.jpg"),   T("k_high_gravel.jpg"),
                         0.52f,0.50f,0.36f,  0.66f,0.62f,0.44f}},
    {"watcher_rim",     {T("k_high_rock.jpg"),    T("k_high_earth.jpg"),
                         T("k_volc_ash.jpg"),     T("k_high_gravel.jpg"),
                         0.28f,0.26f,0.24f,  0.38f,0.36f,0.32f}},
};
static constexpr int kBiomeCount = (int)(sizeof(kBiomeTable)/sizeof(kBiomeTable[0]));

// Default — canyon sandstone
static const BiomeDef kBiomeDefault = {
    T("k_canyon_plain.jpg"), T("k_canyon_cliff.jpg"),
    T("k_canyon_sand.jpg"),  T("k_canyon_layer.jpg"),
    0.38f,0.32f,0.22f,  0.55f,0.44f,0.28f
};

inline const BiomeDef& BiomeDef::ForZone(const char* zone_slug) {
    if (!zone_slug || !zone_slug[0]) return kBiomeDefault;
    for (int i = 0; i < kBiomeCount; ++i)
        if (!strcmp(kBiomeTable[i].slug, zone_slug)) return kBiomeTable[i].def;
    return kBiomeDefault;
}

#undef T
