#pragma once
// md_alloc.h — Hot-patchable allocator function pointers.
//
// Port of VBfA pattern: DAT_0174b750/DAT_0174b754 runtime overrides.
// Defaults to aligned_alloc/free. Override before engine Init() for:
//   - memory tracking / leak detection (debug builds)
//   - custom arena in tools
//   - sanitizer wrappers
//
// Usage:
//   md::SetAllocator(my_alloc, my_free);   // before MdInit()
//   void* p = md::Alloc(256, 16);          // 256 bytes, 16-byte aligned
//   md::Free(p);

#include <cstddef>
#include <cstdlib>

namespace md {

using AllocFn = void*(*)(size_t bytes, size_t align);
using FreeFn  = void (*)(void* ptr);

namespace detail {

inline void* default_alloc(size_t bytes, size_t align) {
#if defined(_MSC_VER)
    return _aligned_malloc(bytes, align);
#else
    if (align < sizeof(void*)) align = sizeof(void*);
    return aligned_alloc(align, (bytes + align - 1) & ~(align - 1));
#endif
}

inline void default_free(void* p) {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    free(p);
#endif
}

inline AllocFn& alloc_fn() { static AllocFn fn = default_alloc; return fn; }
inline FreeFn&  free_fn()  { static FreeFn  fn = default_free;  return fn; }

} // namespace detail

inline void SetAllocator(AllocFn a, FreeFn f) {
    detail::alloc_fn() = a ? a : detail::default_alloc;
    detail::free_fn()  = f ? f : detail::default_free;
}

inline void ResetAllocator() {
    detail::alloc_fn() = detail::default_alloc;
    detail::free_fn()  = detail::default_free;
}

inline void* Alloc(size_t bytes, size_t align = 16) {
    return detail::alloc_fn()(bytes, align);
}

inline void Free(void* p) {
    detail::free_fn()(p);
}

} // namespace md
