#pragma once
#include <cstdint>

// ── SaveVersionChain ──────────────────────────────────────────────────────────
// O3DE-inspired per-version upgrade chain for post-load in-memory migrations.
//
// PURPOSE: avoid growing if/is_vN branches in Deserialize() for future versions.
// When adding v11, register a v10→v11 converter instead of adding branches.
//
// HOW IT WORKS:
//   1. Deserialize() reads old binary format (streaming, version-branched as before).
//   2. After fclose(), call RunUpgrades(loaded_version, SAVE_VERSION, &gs).
//   3. Chain finds all converters where from_version in [loaded, current) and
//      calls them in ascending order — upgrading the in-memory struct step by step.
//
// RULE: Streaming branches (is_v7, is_v9, etc.) handle binary layout differences.
//       Converters handle in-memory field defaults and derived data for new fields.
//
// Usage (game/src/ or main.cpp):
//   SaveVersionChain::Get().Register(10, 11, [](void* ctx) {
//       auto& gs = *static_cast<GameState*>(ctx);
//       gs.new_field = compute_default(gs);
//   });
//   // In Deserialize(), after load:
//   SaveVersionChain::Get().RunUpgrades(loaded_ver, SAVE_VERSION, &gs);

using VersionUpgradeFn = void(*)(void* ctx);

struct VersionConverterEntry {
    uint8_t          from_version = 0;
    uint8_t          to_version   = 0;
    VersionUpgradeFn fn           = nullptr;
};

class SaveVersionChain {
public:
    static constexpr int MAX_CONVERTERS = 16;

    static SaveVersionChain& Get() {
        static SaveVersionChain inst;
        return inst;
    }

    // Register a converter from → to (must be from < to).
    // Returns false if full or invalid range.
    bool Register(uint8_t from, uint8_t to, VersionUpgradeFn fn) noexcept {
        if (!fn || from >= to || count_ >= MAX_CONVERTERS) return false;
        // Update existing entry if same from/to pair
        for (int i = 0; i < count_; ++i) {
            if (entries_[i].from_version == from && entries_[i].to_version == to) {
                entries_[i].fn = fn;
                return true;
            }
        }
        entries_[count_++] = { from, to, fn };
        return true;
    }

    // Run all converters needed to upgrade from loaded_version to current_version.
    // Steps through each version in order: loaded → loaded+1 → … → current.
    // ctx is passed to each converter as void* (game casts to its own type).
    void RunUpgrades(uint8_t loaded_version, uint8_t current_version,
                     void* ctx) const noexcept {
        if (loaded_version >= current_version) return;
        for (uint8_t v = loaded_version; v < current_version; ++v) {
            for (int i = 0; i < count_; ++i) {
                if (entries_[i].from_version == v && entries_[i].fn) {
                    entries_[i].fn(ctx);
                    break;  // at most one converter per version step
                }
            }
        }
    }

    void Clear()  noexcept { count_ = 0; }
    int  Count()  const noexcept { return count_; }

private:
    SaveVersionChain() = default;
    VersionConverterEntry entries_[MAX_CONVERTERS] = {};
    int                   count_ = 0;
};
