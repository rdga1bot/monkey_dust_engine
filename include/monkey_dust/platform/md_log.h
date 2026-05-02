#pragma once
#include <cstdio>

// Thin logging abstraction so engine/ does not need to include raylib.h for TraceLog.
// When Raylib is available (default), delegates to TraceLog.
// Standalone/SDL3 builds fall back to fprintf(stderr).
// Use MD_LOG(MD_LOG_WARNING, ...) — UPPER_CASE avoids clashes with member names.
//
// Optional hook (MdLogSetHook): editor consoles register a callback here so they
// capture engine-level logs without SetTraceLogCallback.  The hook receives the
// already-formatted message string.

using MdLogHook = void(*)(int level, const char* msg);
inline MdLogHook& _md_log_hook() { static MdLogHook h = nullptr; return h; }
inline void MdLogSetHook(MdLogHook h) { _md_log_hook() = h; }

#ifndef MD_LOG_NO_RAYLIB
// Forward-declare to avoid pulling in the full raylib.h.
extern "C" void TraceLog(int logLevel, const char* text, ...);
static constexpr int MD_LOG_INFO    = 3;   // LOG_INFO    in raylib
static constexpr int MD_LOG_WARNING = 4;   // LOG_WARNING in raylib
static constexpr int MD_LOG_ERROR   = 5;   // LOG_ERROR   in raylib
#define MD_LOG(level, ...) do { \
    if (_md_log_hook()) { \
        char _mdbuf[256]; snprintf(_mdbuf, sizeof(_mdbuf), __VA_ARGS__); \
        _md_log_hook()((level), _mdbuf); \
    } \
    TraceLog((level), __VA_ARGS__); \
} while(0)
#else
static constexpr int MD_LOG_INFO    = 3;
static constexpr int MD_LOG_WARNING = 4;
static constexpr int MD_LOG_ERROR   = 5;
#define MD_LOG(level, ...) do { \
    char _mdbuf[256]; snprintf(_mdbuf, sizeof(_mdbuf), __VA_ARGS__); \
    if (_md_log_hook()) _md_log_hook()((level), _mdbuf); \
    else fprintf(stderr, "[%s] %s\n", \
        (level) == MD_LOG_ERROR ? "ERROR" : (level) == MD_LOG_WARNING ? "WARN" : "INFO", _mdbuf); \
} while(0)
#endif
