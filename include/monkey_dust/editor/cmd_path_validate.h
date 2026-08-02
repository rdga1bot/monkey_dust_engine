#pragma once
// CmdPathValidate — EDITOR_AUTOMATION_PLAN_v1.md Phase 2.4: central path
// allowlist check for every command/Lua binding that takes a filesystem
// path (Import/Export Scene, md.editor.capture/dump, future authoring
// scripts). Purely lexical — no realpath()/stat() — so it works for paths
// that don't exist yet (Export/Save targets), and rejects traversal by
// construction rather than by resolving-then-checking:
//   1. Reject absolute paths (leading '/') and empty input.
//   2. Reject any ".." path segment, anywhere in the string — not just a
//      leading one. "data/../../../etc/x" is rejected even though the
//      literal string starts with the "data/" prefix below, because the
//      ".." segment is caught first.
//   3. Require the (post ".."-check) path to start with one of the
//      allowlisted root prefixes: "data/", "game/data/", "automation_out/",
//      "scripts/" (trusted first-party authoring-library source, read via
//      md.editor.load_script() — Lua's own dofile/loadfile are sandboxed
//      out, see lua_system.h's doc comment, so that's the only way an
//      --exec scenario can pull in a shared .lua module).
// No heap, no exceptions — returns false with no output on any rejection.
#include <cstring>

inline bool CmdPathValidate(const char* path) {
    if (!path || path[0] == '\0') return false;
    if (path[0] == '/' || path[0] == '~') return false;

    // Reject any ".." path segment (bounded by '/' or string start/end).
    const char* p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' && (p == path || p[-1] == '/') &&
            (p[2] == '/' || p[2] == '\0')) {
            return false;
        }
        ++p;
    }

    static const char* kAllowedRoots[] = { "data/", "game/data/", "automation_out/", "scripts/" };
    for (const char* root : kAllowedRoots) {
        size_t len = strlen(root);
        if (strncmp(path, root, len) == 0) return true;
    }
    return false;
}
