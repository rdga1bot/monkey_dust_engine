#pragma once
#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>

// ── RenderStreamSet (R-5 VBfA pattern) ────────────────────────���──────────────
// 2 command buffers: shadow_cmd (EVSM + blur) + scene_cmd (main frame).
// Submit shadow_cmd first — GPU starts executing shadow passes immediately while
// CPU continues recording scene_cmd (overlap hides shadow latency on mid-tier iGPUs).
//
// VBfA: 2 renderer streams, each capped at MAX_DRAW_CALLS_PER_STREAM=200 draw calls.
// shadow_cmd: moment passes (3 cascades × ~150 NPC draw) → blur compute
// scene_cmd:  cull compute → prepass → geometry → deferred → post
//
// Usage (per frame):
//   RenderStreamSet rs;
//   rs.Acquire(dev);
//   ... record shadow passes on rs.shadow_cmd ...
//   rs.SubmitShadow();                // GPU starts shadows; CPU continues
//   ... record scene on rs.scene_cmd ...
//   rs.SubmitScene();
//
// Invariant: SubmitShadow() MUST be called before SubmitScene().
// If shadow_cmd is null (AcquireGPUCommandBuffer failed), shadow work is skipped.

namespace md {

struct RenderStreamSet {
    SDL_GPUCommandBuffer* shadow_cmd = nullptr;
    SDL_GPUCommandBuffer* scene_cmd  = nullptr;

    // Acquire both command buffers.  Returns false if either acquisition fails.
    bool Acquire(SDL_GPUDevice* dev) {
        shadow_cmd = SDL_AcquireGPUCommandBuffer(dev);
        scene_cmd  = SDL_AcquireGPUCommandBuffer(dev);
        return shadow_cmd && scene_cmd;
    }

    // Submit shadow_cmd; GPU starts executing shadow passes immediately.
    // Safe to call even if shadow_cmd is null.
    void SubmitShadow() {
        if (shadow_cmd) {
            SDL_SubmitGPUCommandBuffer(shadow_cmd);
            shadow_cmd = nullptr;
        }
    }

    // Submit scene_cmd.  Call after SubmitShadow().
    void SubmitScene() {
        if (scene_cmd) {
            SDL_SubmitGPUCommandBuffer(scene_cmd);
            scene_cmd = nullptr;
        }
    }

    // Emergency cancel: release both without submitting (e.g. swapchain unavailable).
    void Cancel(SDL_GPUDevice* dev) {
        if (shadow_cmd) {
            SDL_CancelGPUCommandBuffer(shadow_cmd);
            shadow_cmd = nullptr;
        }
        if (scene_cmd) {
            SDL_CancelGPUCommandBuffer(scene_cmd);
            scene_cmd = nullptr;
        }
        (void)dev;
    }
};

} // namespace md
#endif // MD_SDL_GPU
