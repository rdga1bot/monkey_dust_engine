#pragma once
// RenderPassGraph — lightweight declarative render pass registry.
//
// Sits alongside RenderTierSystem: tier gates hardware capability,
// pass graph gates individual passes via config or runtime toggle.
//
// Passes are registered with a default enabled state.
// JSON override: data/render_settings.json "passes" section.
// Format:
//   { "passes": { "shadow": true, "ssao": false, "deferred_lighting": true } }
//
// Missing keys use the default provided at Register() time.
// MAX_PASSES = 16 (covers all current and planned render passes).
//
// Typical usage:
//   // At engine init (before game loop):
//   RenderPassGraph::Get().Register("shadow",            true);
//   RenderPassGraph::Get().Register("ssao",              true);
//   RenderPassGraph::Get().Register("deferred_lighting", true);
//   RenderPassGraph::Get().LoadFromJSON("data/render_settings.json");
//
//   // In render loop:
//   if (RenderTierSystem::Get().IsDeferred() &&
//       RenderPassGraph::Get().IsEnabled("shadow"))  { ... }

#include <cstdint>

namespace md {

struct RenderPassEntry {
    char     name[32] = {};
    uint32_t hash     = 0;    // FNV-1a of name — for O(1) hot-path lookup
    bool     enabled  = true;
};

class RenderPassGraph {
public:
    static RenderPassGraph& Get();

    static constexpr int MAX_PASSES = 16;

    // Register a pass; returns false if MAX_PASSES exceeded or name already exists.
    // Call once at startup before LoadFromJSON().
    bool Register(const char* name, bool default_enabled = true);

    // Load enable/disable overrides from the "passes" section in a JSON file.
    // Missing passes keep their registered default. Non-existent file is a no-op.
    // Can be called repeatedly (e.g. on hot-reload) — previous state is preserved
    // for passes not present in the JSON.
    bool LoadFromJSON(const char* path);

    // Query pass enabled state. O(1) via FNV hash.
    bool IsEnabled(const char* name) const;
    bool IsEnabled(uint32_t name_hash) const;

    // Runtime toggle (debug menu, CLI, etc.).
    void SetEnabled(const char* name, bool enabled);
    void SetEnabled(uint32_t name_hash, bool enabled);

    // Iteration for debug UI.
    int                     PassCount()         const { return count_; }
    const RenderPassEntry&  GetPass(int idx)    const { return passes_[idx]; }

    // Reset to registered defaults (undo all JSON / runtime overrides).
    void Reset();

private:
    static uint32_t Hash(const char* s);

    RenderPassEntry passes_[MAX_PASSES] = {};
    bool            defaults_[MAX_PASSES] = {};  // original Register() defaults
    int             count_ = 0;
};

} // namespace md
