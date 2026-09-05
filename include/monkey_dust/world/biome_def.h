#pragma once
#include <cstdint>

// Generic, data-driven biome registry. Contains NO game/IP-specific content —
// real per-biome data (ground texture paths, colours, per-zone parameters)
// is supplied at runtime via BiomeRegistry::Get().LoadFromFile(), pointing
// at a private data file outside this repo. See engine/src/world/biome_registry.cpp
// for the (also generic) file-format parser and lookup implementation.

struct BiomeDef {
    int  tex_base, tex_slope, tex_cliff;   // indices into BiomeRegistry's ground texture table
    int  tex_grass, tex_dirt, tex_road;    // per-biome ground layer indices
    float fog_r, fog_g, fog_b;
    float sky_horizon_r, sky_horizon_g, sky_horizon_b;
    // Real per-biome cliff UV tiling scale (Kenshi FCS "tiling X/Y 2",
    // confirmed against tmp_/kenshi_re/materials/deferred/terrainfp4.hlsl's
    // computeBiome() -- texCoords.yz/xz * scales0.zw). Multiplies the
    // shared world/5000 base coordinate (terrain.hlsl's main_vs); defaults
    // to 1.0 for any biome_table.txt predating this field (see
    // BiomeRegistry::LoadFromFile).
    float cliff_tiling_x = 1.0f, cliff_tiling_y = 1.0f;
    // Real per-biome albedo multiplier (Kenshi FCS "brightness fix",
    // confirmed against terrainfp4.hlsl:212 -- biome.albedo.rgb *=
    // brightnessFix.x). Defaults to 1.0 (no-op) for any biome_table.txt
    // predating this field.
    float brightness_fix = 1.0f;
    // Real per-layer UV tiling scale (Kenshi FCS "tiling X/Y", confirmed
    // against terrainfp4.hlsl's computeBiome() -- texCoords.xy * scales1.xy
    // for base, scales1.zw for grass, scales2.xy for dirt, scales2.zw for
    // road). biome_table.txt already carried these columns (tile_base_x/y
    // etc, task-terrain-brightness header) but BiomeRegistry::LoadFromFile
    // only ever kept cliff_tiling_x/y out of the 12-field tiling block --
    // task #12 (2026-09-03, "ground-texture realism") wires up the other 4
    // pairs so each layer tiles at its OWN real scale instead of one shared
    // DETAIL_TILING=90 constant for everything. Defaults to 1.0 (no-op) for
    // any biome_table.txt predating this field.
    float tile_base_x = 1.0f, tile_base_y = 1.0f;
    float tile_grass_x = 1.0f, tile_grass_y = 1.0f;
    float tile_dirt_x = 1.0f, tile_dirt_y = 1.0f;
    float tile_road_x = 1.0f, tile_road_y = 1.0f;
    // Real "wavy cliff lines" vertical-UV distortion (Kenshi FCS "distort
    // amplitude"/"distort wavelength", confirmed 2026-09-04 via live Ghidra
    // decompile of kenshi_x64.exe's ZoneMap::getTerrainMaterial_DX11, and
    // against tmp_/kenshi_re/materials/deferred/terrain.hlsl's main_vs).
    // freq = wavelength>0 ? 1/wavelength : 0; amp = distort_amplitude*0.01;
    // offset = (cos(worldX*freq)+cos(worldZ*freq))*amp, added to the cliff
    // layer's vertical mapping coordinate. Defaults to 0.0 (no-op) for any
    // biome_table.txt predating this field.
    float distort_amplitude = 0.0f, distort_wavelength = 0.0f;
};

class BiomeRegistry {
public:
    static BiomeRegistry& Get() { static BiomeRegistry inst; return inst; }

    static constexpr int MAX_BIOMES      = 128;
    static constexpr int MAX_TEXTURES    = 256;
    static constexpr int MAX_SLUG_LEN    = 48;
    static constexpr int MAX_PATH_LEN    = 160;

    // Parses the simple text format documented at the top of
    // biome_registry.cpp. Returns false (logs a warning, leaves any
    // previously-loaded state intact) if the file is missing/malformed.
    bool LoadFromFile(const char* path);

    bool Loaded() const { return biome_count_ > 0; }

    // Exact slug match; falls back to biome index 0 (or a zeroed BiomeDef
    // if nothing was ever loaded) if not found.
    const BiomeDef& ForZone(const char* zone_slug) const;

    // Nearest-colour match against each biome's registered legend RGB —
    // the real Kenshi biomemap.png lookup mechanism. Falls back the same way.
    const BiomeDef& ForColor(uint8_t r, uint8_t g, uint8_t b) const;

    int GroundTexCount() const { return tex_count_; }
    const char* GroundTexPath(int idx) const;
    const char* GroundNmlPath(int idx) const;

private:
    BiomeRegistry() = default;

    struct BiomeEntry {
        char slug[MAX_SLUG_LEN];
        BiomeDef def;
        uint8_t legend_rgb[3];
    };

    BiomeEntry biomes_[MAX_BIOMES];
    int        biome_count_ = 0;
    BiomeDef   default_{};

    char tex_paths_[MAX_TEXTURES][MAX_PATH_LEN];
    char nml_paths_[MAX_TEXTURES][MAX_PATH_LEN];
    int  tex_count_ = 0;
};
