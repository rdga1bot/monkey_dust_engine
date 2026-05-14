#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#include <cstdint>

// ── RenderTier ────────────────────────────────────────────────────────────────
// Gates M27-M32 deferred pipeline features.
// Values intentionally ordered: higher = more capable.
enum class RenderTier : uint8_t {
    Forward      = 0,  // Intel HD 520 (24 EU, 25.6 GB/s): forward only
    Deferred_Low = 1,  // Iris 540, Vega 8: GBuffer + deferred lights, no SSAO
    Deferred_Med = 2,  // Vega 11: + SSAO half-res compute
    Deferred_High= 3,  // Iris Plus 640/650: + eDRAM GBuffer fit (~1.4 TB/s)
};

// PCI-ID based tier lookup (per CLAUDE_SDL_GPU_PREP_AI.md §I).
// vendor: 0x8086=Intel  0x1002=AMD  0x10DE=NVIDIA
// vram_mb: 0 = unknown (uses conservative heuristic)
RenderTier DetectTier(uint16_t vendor_id, uint16_t device_id, uint32_t vram_mb);

// ── RenderTierSystem ──────────────────────────────────────────────────────────
// Singleton. Call Detect() once after GpuDevice::Init().
// All M27-M32 systems call GetTier() to gate their features.
namespace md {

class RenderTierSystem {
public:
    static RenderTierSystem& Get();

    // Auto-detect: sysfs PCI IDs (Linux) → SDL device name → conservative fallback.
    // json_override_path: optional, e.g. "data/render_settings.json".
    // JSON wins over auto-detect if present and valid.
    RenderTier Detect(SDL_GPUDevice* dev, const char* json_override_path = nullptr);

    // Hard override (e.g. from CLI arg or game settings menu).
    void       SetTier(RenderTier t) { tier_ = t; }
    RenderTier GetTier()       const { return tier_; }

    bool IsDeferred()  const { return tier_ >= RenderTier::Deferred_Low; }
    bool HasSSAO()     const { return tier_ >= RenderTier::Deferred_Med; }
    bool HaseDRAM()    const { return tier_ == RenderTier::Deferred_High; }

    const char* TierName() const;

private:
    RenderTierSystem() = default;
    RenderTier tier_ = RenderTier::Forward;
};

} // namespace md

#endif // MD_SDL_GPU
