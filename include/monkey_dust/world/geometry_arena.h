#pragma once
#include <cstdlib>
#include <cstdint>

// ── GeometryArena (M-1 VBfA pattern) ─────────────────────────────────────────
// VBfA line 77998: _malloc(0x9600000) = 150MB at level init; free at unload.
// One large arena eliminates thousands of small geometry malloc calls.
// Bump-pointer allocator: O(1) alloc, O(1) reset (no per-object free).
//
// Usage:
//   GeometryArena::Get().Init();      // at level load (once per level)
//   void* p = arena.Alloc(size, 16);  // bump-pointer, 16B aligned
//   GeometryArena::Get().Reset();     // at level unload (keeps memory, resets cursor)
//   GeometryArena::Get().Shutdown();  // at game exit
//
// Sized at 64MB (vs VBfA 150MB) — scaled for Intel HD 520 shared RAM budget.

class GeometryArena {
public:
    static GeometryArena& Get() { static GeometryArena inst; return inst; }

    static constexpr int ARENA_SIZE_BYTES = 64 * 1024 * 1024;  // 64MB

    void Init() {
        if (base_) return;  // already initialised
        base_ = (uint8_t*)malloc(ARENA_SIZE_BYTES);
        used_ = 0;
        owns_ = (base_ != nullptr);
    }

    // Bump-pointer alloc with alignment. Returns nullptr if arena full.
    // Fallback to malloc() if arena not initialised or exhausted.
    void* Alloc(uint32_t bytes, uint32_t align = 16) {
        if (!base_) return malloc(bytes);  // not initialised → safe fallback
        uint32_t off = (used_ + align - 1u) & ~(align - 1u);
        if (off + bytes > (uint32_t)ARENA_SIZE_BYTES)
            return malloc(bytes);           // exhausted → safe fallback
        used_ = off + bytes;
        return base_ + off;
    }

    // O(1) reset — reuse same memory without free/malloc.
    void Reset() { used_ = 0; }

    void Shutdown() {
        if (owns_ && base_) { free(base_); base_ = nullptr; }
        used_ = 0; owns_ = false;
    }

    bool   IsReady()  const { return base_ != nullptr; }
    int    UsedBytes()const { return (int)used_; }
    int    FreeBytes()const { return ARENA_SIZE_BYTES - (int)used_; }
    float  UsedPct()  const { return 100.f * (float)used_ / ARENA_SIZE_BYTES; }

private:
    GeometryArena() = default;
    uint8_t*  base_ = nullptr;
    uint32_t  used_ = 0;
    bool      owns_ = false;
};
