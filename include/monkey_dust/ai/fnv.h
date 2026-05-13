#pragma once
#include <cstdint>

// FNV-1a 32-bit — compile-time and runtime key hashing.
// Used by AgentBlackboard (M18) and FlowGraph (M23).
// Replaces CATHODE SHA1-based ShortGuid; single-pass, no deps.
namespace md {

constexpr uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) noexcept {
    return *s ? fnv1a(s + 1, (h ^ static_cast<uint8_t>(*s)) * 16777619u) : h;
}

inline uint32_t fnv1a_rt(const char* s) noexcept {
    uint32_t h = 2166136261u;
    while (*s) { h = (h ^ static_cast<uint8_t>(*s++)) * 16777619u; }
    return h;
}

} // namespace md
