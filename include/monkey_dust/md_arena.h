#pragma once
#include <cstdint>
#include <cstddef>
#include <monkey_dust/platform/md_log.h>

// MdArena<CAPACITY>: non-owning bump allocator.
// No individual free — reset() reclaims the whole arena.
// Thread-unsafe; designed for single-tick or load-phase use.
template<uint32_t CAPACITY>
class MdArena {
public:
    explicit MdArena(const char* name = "") noexcept : used_(0), name_(name) {}

    void* alloc(uint32_t size, uint32_t align = 8) noexcept {
        uint32_t pad  = (align - (used_ % align)) % align;
        uint32_t next = used_ + pad + size;
        if (next > CAPACITY) {
            MD_LOG(MD_LOG_WARNING, "MdArena '%s': out of memory (need %u, slack %u)",
                   name_, size, CAPACITY - used_);
            return nullptr;
        }
        void* ptr = data_ + used_ + pad;
        used_ = next;
        return ptr;
    }

    template<typename T, typename... Args>
    T* make(Args&&... args) noexcept {
        void* mem = alloc(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return ::new (mem) T(static_cast<Args&&>(args)...);
    }

    void     reset()    noexcept { used_ = 0; }
    uint32_t used()     const noexcept { return used_; }
    uint32_t capacity() const noexcept { return CAPACITY; }
    uint32_t slack()    const noexcept { return CAPACITY - used_; }
    const char* name()  const noexcept { return name_; }

private:
    alignas(16) uint8_t data_[CAPACITY];
    uint32_t            used_;
    const char*         name_;
};

// MdArenaSlot: type-erased view of any MdArena<N>.
// Stores function pointers instead of virtuals — zero vtable overhead.
struct MdArenaSlot {
    const char* name       = "";
    void*       arena_ptr  = nullptr;
    void   (*reset_fn   )(void*)                    = nullptr;
    void*  (*alloc_fn   )(void*, uint32_t, uint32_t) = nullptr;
    uint32_t (*used_fn  )(const void*)              = nullptr;
    uint32_t (*cap_fn   )(const void*)              = nullptr;

    bool     valid()                              const noexcept { return arena_ptr != nullptr; }
    void     reset()                              const noexcept { reset_fn(arena_ptr); }
    void*    alloc(uint32_t sz, uint32_t al = 8) const noexcept { return alloc_fn(arena_ptr, sz, al); }
    uint32_t used()                               const noexcept { return used_fn(arena_ptr); }
    uint32_t capacity()                           const noexcept { return cap_fn(arena_ptr); }
};

template<uint32_t N>
inline MdArenaSlot md_make_arena_slot(MdArena<N>& arena) noexcept {
    MdArenaSlot s;
    s.name      = arena.name();
    s.arena_ptr = &arena;
    s.reset_fn  = [](void* p){ static_cast<MdArena<N>*>(p)->reset(); };
    s.alloc_fn  = [](void* p, uint32_t sz, uint32_t al) -> void* {
        return static_cast<MdArena<N>*>(p)->alloc(sz, al);
    };
    s.used_fn   = [](const void* p){ return static_cast<const MdArena<N>*>(p)->used(); };
    s.cap_fn    = [](const void* p){ return static_cast<const MdArena<N>*>(p)->capacity(); };
    return s;
}

// MdArenaRegistry: Meyers singleton; up to MAX_ARENAS named arenas.
// Inspired by CATHODE MemoryPool + MemoryTracker pattern.
class MdArenaRegistry {
public:
    static constexpr uint8_t MAX_ARENAS = 8;

    static MdArenaRegistry& Get() noexcept {
        static MdArenaRegistry inst;
        return inst;
    }

    template<uint32_t N>
    void Register(uint8_t idx, MdArena<N>& arena) noexcept {
        if (idx >= MAX_ARENAS) {
            MD_LOG(MD_LOG_WARNING, "MdArenaRegistry::Register: idx %u out of range", (unsigned)idx);
            return;
        }
        slots_[idx] = md_make_arena_slot(arena);
    }

    void Unregister(uint8_t idx) noexcept {
        if (idx < MAX_ARENAS) slots_[idx] = {};
    }

    void UnregisterAll() noexcept {
        for (auto& s : slots_) s = {};
    }

    MdArenaSlot& operator[](uint8_t idx) noexcept {
        static MdArenaSlot null_slot{};
        if (idx >= MAX_ARENAS) return null_slot;
        return slots_[idx];
    }

    const MdArenaSlot& operator[](uint8_t idx) const noexcept {
        static const MdArenaSlot null_slot{};
        if (idx >= MAX_ARENAS) return null_slot;
        return slots_[idx];
    }

    void ResetAll() noexcept {
        for (auto& s : slots_) if (s.valid()) s.reset();
    }

    uint8_t count() const noexcept {
        uint8_t n = 0;
        for (const auto& s : slots_) n += s.valid() ? 1 : 0;
        return n;
    }

private:
    MdArenaSlot slots_[MAX_ARENAS] = {};
};
