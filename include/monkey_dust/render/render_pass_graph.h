#pragma once
// RenderPassGraph — declarative render pass registry with resource dependency tracking.
//
// Two layers:
//   1. Enable/disable per pass (existing) — JSON override, runtime toggle.
//   2. Resource declarations — each pass declares reads/writes of named resources.
//      Used to validate ordering and document data-flow between passes.
//      SDL_GPU handles actual VkImageLayout barriers internally; this layer
//      provides organisation, validation, and future automatic reordering.
//
// Resource access model:
//   DeclareRead(pass, resource)  — pass samples this texture as input
//   DeclareWrite(pass, resource) — pass renders into this texture
//
// Validation (Validate()):
//   For each pass P reading resource R, ensures the pass that last wrote R
//   is registered BEFORE P. Logs warnings for violations (soft — SDL_GPU
//   still handles barriers; violations are ordering intent mismatches).
//
// Execution order (GetExecutionOrder()):
//   Returns pass hashes in dependency-respecting order (topological sort).
//   Currently based on registration order + dependency constraints.
//
// JSON format (unchanged):
//   { "passes": { "shadow": true, "ssao": false } }
//
// MAX_PASSES = 16, MAX_RESOURCE_BINDINGS = 4 per pass.
//
// Render graph step 3.2: resource declarations above were pure name-hash
// tokens with no live GPU handle (render-graph audit finding — see
// docs/FULL_AUDIT.md). DeclareTextureDesc/ResolveTexture/GetTexture add a
// companion table (RGResourceEntry, keyed by the SAME resource_hash) that
// actually owns a texture via GpuTexturePool — so a pass can ask this graph
// for its input/output texture instead of each system managing its own
// SDL_GPUTexture* independently.

#include <cstdint>
#include <monkey_dust/render/gpu_texture_pool.h>

namespace md {

// ── Resource access type ──────────────────────────────────────────────────────
enum class RGAccess : uint8_t {
    Read,        // sampled as texture input (e.g. shadow map read by main pass)
    Write,       // color / depth render target (exclusive write)
    ReadWrite,   // load + store (e.g. accumulate over existing content)
};

// Per-pass resource binding declaration.
struct RGResourceBinding {
    uint32_t resource_hash = 0;
    RGAccess access        = RGAccess::Read;
};

// ── Pass entry ────────────────────────────────────────────────────────────────
struct RenderPassEntry {
    char     name[32] = {};
    uint32_t hash     = 0;    // FNV-1a of name — O(1) hot-path lookup
    bool     enabled  = true;

    // Resource dependency declarations (up to MAX_RESOURCE_BINDINGS each).
    static constexpr int MAX_RESOURCE_BINDINGS = 4;
    RGResourceBinding reads [MAX_RESOURCE_BINDINGS] = {};
    RGResourceBinding writes[MAX_RESOURCE_BINDINGS] = {};
    int               read_count  = 0;
    int               write_count = 0;
};

// ── Resource entry (step 3.2: live texture handle) ────────────────────────────
// Companion table to RGResourceBinding, keyed by the same FNV-1a hash — the
// binding struct above stays hash-only (unchanged, still just validates
// ordering); this struct is where an actual GpuTexturePool-backed texture
// lives once a pass calls ResolveTexture() for that resource name.
struct RGResourceEntry {
    uint32_t        hash    = 0;
    RGTextureDesc   desc    = {};
    SDL_GPUTexture* live_tex = nullptr;
};

// ── Singleton registry ────────────────────────────────────────────────────────
class RenderPassGraph {
public:
    static RenderPassGraph& Get();

    static constexpr int MAX_PASSES = 16;

    // Register a pass. Returns false if MAX_PASSES exceeded or duplicate.
    bool Register(const char* name, bool default_enabled = true);

    // Declare resource access for ordering validation.
    // Call after Register(), before the render loop.
    // Unknown pass names are silently ignored (graceful degradation).
    void DeclareRead (const char* pass_name, const char* resource_name);
    void DeclareWrite(const char* pass_name, const char* resource_name);

    // Validate declared ordering: for each pass P reading resource R,
    // verifies the writing pass of R is registered before P.
    // Returns false and logs if violations found. Call after all declarations.
    bool Validate() const;

    // Load enable/disable overrides from "passes" section in JSON.
    // Non-existent file is a no-op; missing keys keep registered defaults.
    bool LoadFromJSON(const char* path);

    // Query enabled state — O(1) via FNV hash.
    bool IsEnabled(const char* name) const;
    bool IsEnabled(uint32_t name_hash) const;

    // Runtime toggle.
    void SetEnabled(const char* name, bool enabled);
    void SetEnabled(uint32_t name_hash, bool enabled);

    // Iteration.
    int                     PassCount()      const { return count_; }
    const RenderPassEntry&  GetPass(int idx) const { return passes_[idx]; }

    // Reset to registered defaults.
    void Reset();

    // ── Step 3.2: live resource handles ──────────────────────────────────────
    static constexpr int MAX_RESOURCES = 32;

    // Register (or update) a resource's texture description. Call once per
    // resource name before the first ResolveTexture() for it — typically
    // right after the DeclareRead/DeclareWrite calls that reference it.
    void DeclareTextureDesc(const char* resource_name, const RGTextureDesc& desc);

    // Acquires (from GpuTexturePool) and caches the live texture for a
    // resource this frame. Safe to call multiple times per frame for the
    // same name (returns the same cached handle) — only the first call in
    // a frame actually hits the pool. Returns nullptr if no desc was
    // declared for this name, or the pool is exhausted.
    SDL_GPUTexture* ResolveTexture(SDL_GPUDevice* dev, const char* resource_name);

    // Returns the currently-resolved texture for a resource, or nullptr if
    // ResolveTexture hasn't been called for it yet this frame.
    SDL_GPUTexture* GetTexture(const char* resource_name) const;

    // Releases every resolved texture back to GpuTexturePool and clears the
    // cached handles. Call once per frame, after all passes have run.
    void ReleaseFrame();

private:
    static uint32_t Hash(const char* s);
    int FindByHash(uint32_t h) const;
    int FindResourceByHash(uint32_t h) const;

    RenderPassEntry passes_ [MAX_PASSES] = {};
    bool            defaults_[MAX_PASSES] = {};
    int             count_ = 0;

    RGResourceEntry resources_[MAX_RESOURCES] = {};
    int             resource_count_ = 0;
};

} // namespace md
