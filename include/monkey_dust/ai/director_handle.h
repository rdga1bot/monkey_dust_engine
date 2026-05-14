#pragma once
#include <cstdint>
#include <monkey_dust/platform/md_log.h>

// DirectorHandle — CATHODE GlobalSingleton weak-ref pattern:
// Entities hold a DirectorHandle (8 bytes) instead of a raw pointer.
// The handle is valid only while the DirectorSystem's generation matches.
// On level unload, generation is bumped → all handles become invalid.
// Zero-cost to check; no heap; no weak_ptr overhead.
//
// Usage:
//   DirectorHandleToken tok = DirectorSystem::CurrentToken();
//   if (DirectorHandle::IsValid(tok)) { ... }

struct DirectorHandleToken {
    uint32_t generation = 0;   // bumped on each DirectorSystem::Shutdown()
    uint32_t _pad       = 0;
};
static_assert(sizeof(DirectorHandleToken) == 8);

class DirectorHandle {
public:
    static constexpr uint32_t INVALID_GEN = 0u;

    static DirectorHandle& Get() noexcept {
        static DirectorHandle inst;
        return inst;
    }

    // Called by DirectorSystem::Init().
    void Activate() noexcept {
        ++generation_;
        if (!generation_) ++generation_;  // skip 0
        active_ = true;
    }

    // Called by DirectorSystem::Shutdown() — invalidates all existing tokens.
    // Generation is NOT reset; Activate() will bump it again on next Init().
    void Invalidate() noexcept { active_ = false; }

    // Obtain a token representing the current session.
    DirectorHandleToken Token() const noexcept {
        return { generation_, 0u };
    }

    // Check if a previously-obtained token is still valid.
    bool IsValid(const DirectorHandleToken& tok) const noexcept {
        return active_ &&
               tok.generation != INVALID_GEN &&
               tok.generation == generation_;
    }

    uint32_t generation() const noexcept { return generation_; }

private:
    uint32_t generation_ = INVALID_GEN;
    bool     active_     = false;
};

// ECS component: each entity that listens to the Director holds one of these.
struct DirectorListenerComponent {
    DirectorHandleToken token;
    bool wants_hints;    // true = entity polls DirectorHintComponent each tick
    uint8_t _pad[3];
};
static_assert(sizeof(DirectorListenerComponent) == 12);
