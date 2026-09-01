#pragma once
// GpuTexturePool — unified transient GPU texture allocator (render graph
// step 3.1). Consolidates ~9 independent, hand-rolled SDL_CreateGPUTexture
// call sites (GBuffer, DeferredLightingSystem, BloomSystem, MotionBlurSystem,
// CasPass, OitPass, SSAOSystem, EvsmShadow — see docs/FULL_AUDIT.md
// render-target inventory) with a single, reusable factory + pool.
//
// Not yet wired into any of those systems (that's step 3.3) — this class is
// additive infrastructure, safe to introduce without touching live render
// code. A future pass can migrate each system's Init()/Shutdown() to
// Acquire()/Release() one at a time.
//
// Fixed-size, no heap (MAX_POOLED=32) — matches the project's no-STL-
// container-in-hot-path convention (same pattern as PipeCache/SpvCache).

#include <SDL3/SDL_gpu.h>

namespace md {

struct RGTextureDesc {
    int                       width  = 0;
    int                       height = 0;
    SDL_GPUTextureFormat      format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureUsageFlags  usage  = 0;
    const char*               debug_name = "";

    bool operator==(const RGTextureDesc& o) const {
        return width == o.width && height == o.height &&
               format == o.format && usage == o.usage;
    }
};

class GpuTexturePool {
public:
    static GpuTexturePool& Get() { static GpuTexturePool inst; return inst; }

    // Returns a texture matching desc — reuses a released, matching pool
    // entry if one exists, otherwise creates a new SDL_GPUTexture. Returns
    // nullptr (logs a warning) if the pool is full and none can be reused.
    SDL_GPUTexture* Acquire(SDL_GPUDevice* dev, const RGTextureDesc& desc);

    // Marks a previously-Acquired texture as free for reuse by a later
    // Acquire() with a matching desc. Does NOT destroy the GPU resource —
    // that only happens in Shutdown().
    void Release(SDL_GPUTexture* tex);

    // Call once per frame after all passes have run. Any entry still
    // marked in-use (a caller forgot to Release) is force-freed and logged
    // — a safety net, not a correctness requirement of well-behaved callers.
    void EndFrame();

    // Releases every pooled GPU texture. Call at shutdown.
    void Shutdown();

    static constexpr int MAX_POOLED = 32;

private:
    GpuTexturePool() = default;

    struct Entry {
        SDL_GPUTexture* tex    = nullptr;
        RGTextureDesc   desc   = {};
        bool            in_use = false;
    };

    Entry entries_[MAX_POOLED] = {};
    int   count_ = 0;
    SDL_GPUDevice* dev_ = nullptr;  // cached from first Acquire(), used by Shutdown()
};

} // namespace md
