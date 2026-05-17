#include <monkey_dust/render/render_pass_graph.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstdio>
#include <cstring>

namespace md {

// ── FNV-1a 32-bit (mirrors fnv.h runtime variant, no dependency) ─────────────

uint32_t RenderPassGraph::Hash(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
    return h ? h : 1u;
}

// ── Singleton ────────────────────────────────────────────────────────────────

RenderPassGraph& RenderPassGraph::Get() {
    static RenderPassGraph inst;
    return inst;
}

// ── Register ─────────────────────────────────────────────────────────────────

bool RenderPassGraph::Register(const char* name, bool default_enabled) {
    if (!name || !name[0]) return false;
    if (count_ >= MAX_PASSES) {
        fprintf(stderr, "[RenderPassGraph] MAX_PASSES=%d reached, cannot register '%s'\n",
                MAX_PASSES, name);
        return false;
    }
    uint32_t h = Hash(name);
    // Prevent duplicates.
    for (int i = 0; i < count_; ++i)
        if (passes_[i].hash == h) return false;

    auto& e = passes_[count_];
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.hash    = h;
    e.enabled = default_enabled;
    defaults_[count_] = default_enabled;
    ++count_;
    return true;
}

// ── LoadFromJSON ─────────────────────────────────────────────────────────────
// Parses the "passes" section from data/render_settings.json.
// Format:
//   { "passes": { "shadow": false, "ssao": true } }
//
// Uses strstr-based parsing (no external JSON library — project invariant).

bool RenderPassGraph::LoadFromJSON(const char* path) {
    if (!path) return false;

    // Read file via md::fs_read_alloc (returns malloc'd buffer).
    uint32_t sz = 0;
    char*    buf = md::fs_read_alloc(path, &sz);
    if (!buf) return false;          // file not found — silent no-op

    const char* passes_sec = strstr(buf, "\"passes\"");
    if (!passes_sec) { md::fs_free(buf); return false; }

    // Find the opening '{' of the passes object.
    const char* obj = strchr(passes_sec + 8, '{');
    if (!obj) { md::fs_free(buf); return false; }

    // Walk through key-value pairs until matching '}'.
    const char* p = obj + 1;
    int changed = 0;
    while (*p && *p != '}') {
        // Skip whitespace and commas.
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) ++p;
        if (*p != '"') { ++p; continue; }

        // Extract key.
        ++p;  // skip opening "
        char key[32] = {};
        int ki = 0;
        while (*p && *p != '"' && ki < 31) key[ki++] = *p++;
        if (*p == '"') ++p;

        // Skip to ':'.
        while (*p && *p != ':' && *p != '}') ++p;
        if (*p == ':') ++p;

        // Skip whitespace.
        while (*p == ' ' || *p == '\t') ++p;

        // Parse bool value.
        bool val = true;
        if (strncmp(p, "true",  4) == 0)  { val = true;  p += 4; }
        else if (strncmp(p, "false", 5) == 0) { val = false; p += 5; }
        else { ++p; continue; }

        // Apply to matching pass.
        uint32_t h = Hash(key);
        for (int i = 0; i < count_; ++i) {
            if (passes_[i].hash == h) {
                if (passes_[i].enabled != val) {
                    passes_[i].enabled = val;
                    ++changed;
                    if (!val)
                        fprintf(stdout, "[RenderPassGraph] '%s' disabled via JSON\n", key);
                }
                break;
            }
        }
    }

    md::fs_free(buf);
    if (changed > 0)
        fprintf(stdout, "[RenderPassGraph] %d pass(es) overridden from '%s'\n", changed, path);
    return true;
}

// ── IsEnabled ────────────────────────────────────────────────────────────────

bool RenderPassGraph::IsEnabled(const char* name) const {
    return IsEnabled(Hash(name));
}

bool RenderPassGraph::IsEnabled(uint32_t h) const {
    for (int i = 0; i < count_; ++i)
        if (passes_[i].hash == h) return passes_[i].enabled;
    return true;  // unknown pass → enabled by default (opt-out model)
}

// ── SetEnabled ───────────────────────────────────────────────────────────────

void RenderPassGraph::SetEnabled(const char* name, bool enabled) {
    SetEnabled(Hash(name), enabled);
}

void RenderPassGraph::SetEnabled(uint32_t h, bool enabled) {
    for (int i = 0; i < count_; ++i) {
        if (passes_[i].hash == h) { passes_[i].enabled = enabled; return; }
    }
}

// ── Reset ────────────────────────────────────────────────────────────────────

void RenderPassGraph::Reset() {
    for (int i = 0; i < count_; ++i)
        passes_[i].enabled = defaults_[i];
}

} // namespace md
