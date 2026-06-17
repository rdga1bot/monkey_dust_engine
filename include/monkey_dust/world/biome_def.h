#pragma once
#include <cstring>
// Biome definitions — per-zone terrain textures (Kenshi DDS layer indices),
// fog colour, and sky horizon colour.

// ─── Per-zone ground texture layer selection ────────────────────────────────
// Each value is an index into kGroundTexPaths[] / the GPU sampler2DArray.
enum GroundTexLayer : int {
    K_DESERT_SAND      = 0,
    K_DESERT_ROCK      = 1,
    K_DESERT_SLABS     = 2,
    K_DESERT_SANDSTONE = 3,
    K_HIGH_BONE        = 4,
    K_HIGH_GRAVEL      = 5,
    K_HIGH_ROCK        = 6,
    K_HIGH_EARTH       = 7,
    K_CANYON_CLIFF     = 8,
    K_CANYON_PLAIN     = 9,
    K_CANYON_SAND      = 10,
    K_CANYON_LAYER     = 11,
    K_VOLC_ASH         = 12,
    K_VOLC_DARK        = 13,
    K_VOLC_FLOOR       = 14,
    K_VOLC_HELL        = 15,
    K_SWAMP_MUD        = 16,
    K_SWAMP_ROCK       = 17,
    K_SWAMP_FLOOR      = 18,
    K_SWAMP_DARK       = 19,
    K_SCRUB_FLAT       = 20,
    K_SCRUB_ROCK       = 21,
    K_SCRUB_DIRT       = 22,
    K_SCRUB_MUD        = 23,
};
static constexpr int kGroundTexCount = 24;

// DDS source paths — ordered by GroundTexLayer; loaded into a single
// sampler2DArray by TerrainRenderer::InitGroundTextureArray().
static const char* const kGroundTexPaths[kGroundTexCount] = {
    "tmp_/kenshi_re/terrain_textures/DesertSands_DIF.dds",                     //  0 K_DESERT_SAND
    "tmp_/kenshi_re/terrain_textures/DesertFloorRock01_DIF.dds",               //  1 K_DESERT_ROCK
    "tmp_/kenshi_re/terrain_textures/Bedded_Sandstone_DIF.dds",                //  2 K_DESERT_SLABS
    "tmp_/kenshi_re/terrain_textures/DesertVarnishSlabs_DIF.dds",              //  3 K_DESERT_SANDSTONE
    "tmp_/kenshi_re/terrain_textures/BoneLoop_DIF_WHITE.dds",                  //  4 K_HIGH_BONE
    "tmp_/kenshi_re/terrain_textures/WadiGravel-WHITE_DIF.dds",                //  5 K_HIGH_GRAVEL
    "tmp_/kenshi_re/terrain_textures/Fractured_Rock_DIF.dds",                  //  6 K_HIGH_ROCK
    "tmp_/kenshi_re/terrain_textures/GritMudROCK_DIF.dds",                     //  7 K_HIGH_EARTH
    "tmp_/kenshi_re/terrain_textures/CanyonCliff01_DIF.dds",                   //  8 K_CANYON_CLIFF
    "tmp_/kenshi_re/terrain_textures/PlainSandstoneLoop_DIF.dds",              //  9 K_CANYON_PLAIN
    "tmp_/kenshi_re/terrain_textures/Crossbedded_Sandstone_DIF.dds",           // 10 K_CANYON_SAND
    "tmp_/kenshi_re/terrain_textures/LayeredSandstone_DIF.dds",                // 11 K_CANYON_LAYER
    "tmp_/kenshi_re/terrain_textures/Ashland_Ash_DIF.dds",                     // 12 K_VOLC_ASH
    "tmp_/kenshi_re/terrain_textures/Ashloop_DIF.dds",                         // 13 K_VOLC_DARK
    "tmp_/kenshi_re/terrain_textures/Darkstone_Floor_DIF.dds",                 // 14 K_VOLC_FLOOR
    "tmp_/kenshi_re/terrain_textures/Hellish_Ground_DIF.dds",                  // 15 K_VOLC_HELL
    "tmp_/kenshi_re/terrain_textures/mudpool_SWAMP_DIF.dds",                   // 16 K_SWAMP_MUD
    "tmp_/kenshi_re/terrain_textures/Landtexture_Swamp_Rocksurface01_DIF.dds", // 17 K_SWAMP_ROCK
    "tmp_/kenshi_re/terrain_textures/SwampFloor_SlopeROOTS_DIF.dds",           // 18 K_SWAMP_FLOOR
    "tmp_/kenshi_re/terrain_textures/Pahoehoe_Swamp_DIF.dds",                  // 19 K_SWAMP_DARK
    "tmp_/kenshi_re/terrain_textures/Flat_Land_DIF.dds",                       // 20 K_SCRUB_FLAT
    "tmp_/kenshi_re/terrain_textures/GroundRocks01_DIF.dds",                   // 21 K_SCRUB_ROCK
    "tmp_/kenshi_re/terrain_textures/Okran_Dirt_DIF.dds",                      // 22 K_SCRUB_DIRT
    "tmp_/kenshi_re/terrain_textures/DriedMud_DIF.dds",                        // 23 K_SCRUB_MUD
};

struct BiomeDef {
    int  tex0, tex1, tex2, tex3;   // GroundTexLayer indices for splat blending
    float fog_r, fog_g, fog_b;
    float sky_horizon_r, sky_horizon_g, sky_horizon_b;

    static const BiomeDef& ForZone(const char* zone_slug);
};

static const struct { const char* slug; BiomeDef def; } kBiomeTable[] = {
    // ── DESERT ──────────────────────────────────────────────────────────────
    {"great_desert",    {K_DESERT_SAND, K_DESERT_ROCK, K_DESERT_SLABS, K_DESERT_SANDSTONE,
                         0.72f,0.60f,0.35f,  0.90f,0.72f,0.40f}},
    {"stenn_desert",    {K_DESERT_SAND, K_DESERT_SANDSTONE, K_DESERT_ROCK, K_DESERT_SLABS,
                         0.75f,0.62f,0.34f,  0.92f,0.75f,0.42f}},
    {"skimsands",       {K_DESERT_SAND, K_DESERT_SLABS, K_DESERT_SANDSTONE, K_DESERT_ROCK,
                         0.78f,0.65f,0.38f,  0.94f,0.78f,0.46f}},
    {"bonefields",      {K_HIGH_BONE, K_DESERT_SAND, K_HIGH_GRAVEL, K_DESERT_ROCK,
                         0.70f,0.65f,0.48f,  0.85f,0.75f,0.55f}},
    {"high_bonefields", {K_HIGH_BONE, K_HIGH_GRAVEL, K_DESERT_SAND, K_HIGH_ROCK,
                         0.68f,0.62f,0.50f,  0.82f,0.72f,0.56f}},
    {"stobe_gamble",    {K_DESERT_ROCK, K_CANYON_CLIFF, K_DESERT_SANDSTONE, K_HIGH_GRAVEL,
                         0.62f,0.55f,0.38f,  0.78f,0.65f,0.44f}},

    // ── VOLCANIC / ASHLANDS ─────────────────────────────────────────────────
    {"ashlands",        {K_VOLC_ASH, K_VOLC_DARK, K_VOLC_FLOOR, K_VOLC_HELL,
                         0.18f,0.14f,0.12f,  0.30f,0.18f,0.10f}},
    {"venge",           {K_VOLC_DARK, K_VOLC_HELL, K_VOLC_ASH, K_VOLC_FLOOR,
                         0.15f,0.10f,0.08f,  0.22f,0.12f,0.08f}},
    {"deadlands",       {K_VOLC_ASH, K_VOLC_FLOOR, K_HIGH_BONE, K_HIGH_GRAVEL,
                         0.22f,0.18f,0.16f,  0.34f,0.24f,0.18f}},

    // ── CANYON / ROCKY ──────────────────────────────────────────────────────
    {"rocky_canyon",    {K_CANYON_PLAIN, K_CANYON_CLIFF, K_CANYON_SAND, K_CANYON_LAYER,
                         0.42f,0.32f,0.22f,  0.62f,0.48f,0.28f}},
    {"dreg",            {K_CANYON_CLIFF, K_CANYON_LAYER, K_SCRUB_ROCK, K_HIGH_ROCK,
                         0.40f,0.34f,0.26f,  0.58f,0.48f,0.32f}},
    {"the_pits",        {K_CANYON_CLIFF, K_VOLC_DARK, K_CANYON_SAND, K_CANYON_LAYER,
                         0.28f,0.24f,0.20f,  0.42f,0.34f,0.24f}},
    {"stobe_garden",    {K_CANYON_SAND, K_CANYON_PLAIN, K_DESERT_SANDSTONE, K_CANYON_LAYER,
                         0.50f,0.42f,0.28f,  0.68f,0.56f,0.34f}},
    {"okran_pride",     {K_SCRUB_FLAT, K_CANYON_PLAIN, K_SCRUB_DIRT, K_DESERT_SANDSTONE,
                         0.55f,0.50f,0.35f,  0.70f,0.62f,0.42f}},

    // ── SWAMP ───────────────────────────────────────────────────────────────
    {"swamplands",      {K_SWAMP_MUD, K_SWAMP_ROCK, K_SWAMP_FLOOR, K_SWAMP_DARK,
                         0.22f,0.30f,0.20f,  0.30f,0.42f,0.26f}},
    {"shem",            {K_SWAMP_MUD, K_SWAMP_FLOOR, K_SWAMP_ROCK, K_SWAMP_DARK,
                         0.20f,0.28f,0.18f,  0.28f,0.40f,0.24f}},
    {"gut",             {K_SWAMP_DARK, K_SWAMP_MUD, K_SWAMP_ROCK, K_SWAMP_FLOOR,
                         0.16f,0.24f,0.16f,  0.22f,0.34f,0.20f}},
    {"greenbeach",      {K_SWAMP_FLOOR, K_SWAMP_MUD, K_SCRUB_FLAT, K_SCRUB_ROCK,
                         0.28f,0.38f,0.26f,  0.38f,0.50f,0.32f}},
    {"floodlands",      {K_SWAMP_MUD, K_SWAMP_DARK, K_SCRUB_MUD, K_SWAMP_ROCK,
                         0.24f,0.32f,0.22f,  0.32f,0.44f,0.28f}},
    {"sonorous_dark",   {K_SWAMP_DARK, K_HIGH_ROCK, K_SCRUB_ROCK, K_VOLC_ASH,
                         0.20f,0.22f,0.18f,  0.28f,0.30f,0.24f}},

    // ── SCRUBLAND ───────────────────────────────────────────────────────────
    {"border_zone",     {K_SCRUB_DIRT, K_SCRUB_ROCK, K_SCRUB_FLAT, K_SCRUB_MUD,
                         0.48f,0.42f,0.28f,  0.62f,0.54f,0.36f}},
    {"cannibal_plains", {K_SCRUB_FLAT, K_SCRUB_DIRT, K_DESERT_ROCK, K_SCRUB_ROCK,
                         0.50f,0.44f,0.26f,  0.65f,0.56f,0.34f}},
    {"skinner_roam",    {K_SCRUB_MUD, K_SCRUB_FLAT, K_SCRUB_ROCK, K_SCRUB_DIRT,
                         0.46f,0.40f,0.26f,  0.60f,0.52f,0.34f}},
    {"the_hub",         {K_SCRUB_DIRT, K_SCRUB_FLAT, K_SCRUB_ROCK, K_DESERT_SANDSTONE,
                         0.52f,0.46f,0.30f,  0.66f,0.58f,0.38f}},
    {"iron_valleys",    {K_SCRUB_ROCK, K_HIGH_ROCK, K_SCRUB_DIRT, K_HIGH_EARTH,
                         0.40f,0.36f,0.28f,  0.52f,0.46f,0.34f}},
    {"howler_maze",     {K_HIGH_ROCK, K_SCRUB_ROCK, K_HIGH_EARTH, K_VOLC_ASH,
                         0.32f,0.30f,0.26f,  0.44f,0.40f,0.34f}},

    // ── HIGHLANDS ───────────────────────────────────────────────────────────
    {"shek_territory",  {K_HIGH_EARTH, K_HIGH_ROCK, K_SCRUB_ROCK, K_HIGH_GRAVEL,
                         0.38f,0.36f,0.32f,  0.50f,0.48f,0.42f}},
    {"the_crags",       {K_HIGH_ROCK, K_HIGH_EARTH, K_CANYON_CLIFF, K_HIGH_GRAVEL,
                         0.35f,0.32f,0.28f,  0.46f,0.42f,0.36f}},
    {"bast",            {K_HIGH_EARTH, K_SCRUB_ROCK, K_HIGH_ROCK, K_HIGH_GRAVEL,
                         0.40f,0.38f,0.32f,  0.52f,0.50f,0.42f}},
    {"the_spine",       {K_HIGH_ROCK, K_HIGH_EARTH, K_VOLC_ASH, K_HIGH_GRAVEL,
                         0.30f,0.28f,0.26f,  0.40f,0.38f,0.34f}},
    {"fog_islands",     {K_HIGH_EARTH, K_SWAMP_ROCK, K_HIGH_ROCK, K_SCRUB_FLAT,
                         0.35f,0.40f,0.38f,  0.45f,0.52f,0.48f}},
    {"leviathan_coast", {K_HIGH_ROCK, K_HIGH_GRAVEL, K_SWAMP_ROCK, K_HIGH_EARTH,
                         0.38f,0.44f,0.42f,  0.48f,0.56f,0.52f}},
    {"holy_nation",     {K_SCRUB_FLAT, K_DESERT_SANDSTONE, K_SCRUB_DIRT, K_HIGH_GRAVEL,
                         0.52f,0.50f,0.36f,  0.66f,0.62f,0.44f}},
    {"watcher_rim",     {K_HIGH_ROCK, K_HIGH_EARTH, K_VOLC_ASH, K_HIGH_GRAVEL,
                         0.28f,0.26f,0.24f,  0.38f,0.36f,0.32f}},
};
static constexpr int kBiomeCount = (int)(sizeof(kBiomeTable)/sizeof(kBiomeTable[0]));

// Default — canyon sandstone
static const BiomeDef kBiomeDefault = {
    K_CANYON_PLAIN, K_CANYON_CLIFF, K_CANYON_SAND, K_CANYON_LAYER,
    0.38f,0.32f,0.22f,  0.55f,0.44f,0.28f
};

inline const BiomeDef& BiomeDef::ForZone(const char* zone_slug) {
    if (!zone_slug || !zone_slug[0]) return kBiomeDefault;
    for (int i = 0; i < kBiomeCount; ++i)
        if (!strcmp(kBiomeTable[i].slug, zone_slug)) return kBiomeTable[i].def;
    return kBiomeDefault;
}
