#pragma once
#include <cstdint>

// ── md::fs — thin platform filesystem abstraction ─────────────────────────────
// Cocos2d-x-inspired "thin platform layer" for file I/O.
// Centralises fopen/fread/fclose so callers never touch FILE* directly.
//
// Design:
//   fs_read_all — load entire file into caller-supplied buffer; no heap alloc
//   fs_read_alloc — load into a malloc'd buffer (caller must call fs_free)
//   fs_free  — free a buffer returned by fs_read_alloc
//   fs_exists — check path existence
//
// All functions are thread-safe (no shared state).
// Engine code must use these wrappers — never raw fopen — for testability
// and future platform portability (Android asset manager, virtual FS, etc.).

namespace md {

// Read entire file into `buf` (max `buf_sz - 1` bytes). Null-terminates.
// Returns number of bytes read, or 0 on error.
uint32_t fs_read_all(const char* path, char* buf, uint32_t buf_sz) noexcept;

// Read entire file into a malloc'd buffer. Caller must call fs_free(buf).
// Returns nullptr on error. Sets *out_size to byte count (excluding null-term).
char* fs_read_alloc(const char* path, uint32_t* out_size) noexcept;

// Free a buffer returned by fs_read_alloc.
void fs_free(char* buf) noexcept;

// Returns true if the file exists and can be opened for reading.
bool fs_exists(const char* path) noexcept;

} // namespace md
