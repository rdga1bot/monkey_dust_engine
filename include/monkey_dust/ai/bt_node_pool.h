#pragma once
#include <monkey_dust/ai/behavior_tree.h>
#include <cstdint>
#include <new>

// ── BTNodePool ────────────────────────────────────────────────────────────────
// Cache-aligned bump allocator for BehaviorTree template objects.
// Mirrors AI BlockAllocator: allocation from 4KB-aligned blocks,
// O(1) Reset (resets used counters without touching allocations).
//
// Primary use: load-time allocation of BT template instances.
// Templates are shared across entities via BehaviorTreeComponent::tree.
// NOT for game-loop use — allocate during map/NPC spawn, not per-frame.
//
// Layout (BSS):
//   [Block 0: 4096 bytes] [Block 1: 4096 bytes] ... [Block MAX_BLOCKS-1]
//   Each block is 64-byte aligned for cache-line locality.
//
// Capacity example (MAX_BLOCKS=8, BLOCK_SIZE=4096):
//   sizeof(BehaviorTree) ≈ 4616 bytes → each BT spans 2 blocks
//   → pool holds up to (MAX_BLOCKS * BLOCK_SIZE) / sizeof(BT) ≈ 7 templates
//
// Usage:
//   BTNodePool pool;
//   BehaviorTree* t = pool.AllocTree();   // placement-new BT in pool
//   // ... build t with t->addSelector() etc. ...
//   pool.Reset();  // destroy all trees, reuse memory
class BTNodePool {
public:
    static constexpr uint32_t BLOCK_SIZE = 4096u;
    static constexpr uint8_t  MAX_BLOCKS = 8u;    // 32 KB total
    static constexpr uint32_t TOTAL_BYTES = MAX_BLOCKS * BLOCK_SIZE;

    BTNodePool() noexcept { Reset(); }

    // Allocate and placement-new a BehaviorTree.
    // Returns nullptr if pool is full.
    BehaviorTree* AllocTree() noexcept {
        void* p = alloc_raw(sizeof(BehaviorTree), alignof(BehaviorTree));
        if (!p) return nullptr;
        return new(p) BehaviorTree();
    }

    // Allocate raw bytes aligned to `align`. Returns nullptr if pool is full.
    // For advanced use — prefer AllocTree() for BT instances.
    template<typename T>
    T* Alloc() noexcept {
        void* p = alloc_raw(sizeof(T), alignof(T));
        if (!p) return nullptr;
        return new(p) T();
    }

    // Reset all blocks: calls ~BehaviorTree on every live allocation, then
    // zeroes usage counters. All previously-returned pointers become invalid.
    void Reset() noexcept {
        for (int i = static_cast<int>(m_alloc_count) - 1; i >= 0; --i) {
            if (m_alloc_types[i] == AllocType::BehaviorTree_)
                static_cast<BehaviorTree*>(m_alloc_ptrs[i])->~BehaviorTree();
        }
        m_alloc_count     = 0;
        m_flat_offset     = 0;
        m_total_allocated = 0;
    }

    uint32_t BytesUsed()      const noexcept { return m_total_allocated; }
    uint32_t BytesRemaining() const noexcept {
        return (m_flat_offset < TOTAL_BYTES) ? TOTAL_BYTES - m_flat_offset : 0;
    }

    // Number of BehaviorTree objects currently allocated.
    uint8_t TreeCount() const noexcept {
        uint8_t n = 0;
        for (uint8_t i = 0; i < m_alloc_count; ++i)
            if (m_alloc_types[i] == AllocType::BehaviorTree_) ++n;
        return n;
    }

private:
    enum class AllocType : uint8_t { Generic = 0, BehaviorTree_ = 1 };

    static constexpr uint8_t MAX_ALLOCS = 32;  // track up to 32 allocations

    // Flat 32KB arena — 2D array is contiguous, supports allocations that
    // span across block boundaries (sizeof(BehaviorTree) = 4616 > BLOCK_SIZE).
    alignas(64) uint8_t m_data[MAX_BLOCKS][BLOCK_SIZE];
    uint32_t            m_flat_offset;      // bump pointer into flat arena
    uint32_t            m_total_allocated;

    void*      m_alloc_ptrs [MAX_ALLOCS];
    AllocType  m_alloc_types[MAX_ALLOCS];
    uint8_t    m_alloc_count;

    void* alloc_raw(uint32_t bytes, uint32_t align) noexcept {
        uint8_t* base    = &m_data[0][0];
        uint32_t aligned = (m_flat_offset + align - 1u) & ~(align - 1u);
        if (aligned + bytes > TOTAL_BYTES) return nullptr;
        void* ptr         = base + aligned;
        m_flat_offset     = aligned + bytes;
        m_total_allocated += bytes;
        if (m_alloc_count < MAX_ALLOCS) {
            m_alloc_ptrs [m_alloc_count] = ptr;
            m_alloc_types[m_alloc_count] = (bytes == sizeof(BehaviorTree))
                                           ? AllocType::BehaviorTree_
                                           : AllocType::Generic;
            ++m_alloc_count;
        }
        return ptr;
    }
};
