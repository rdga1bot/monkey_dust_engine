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
