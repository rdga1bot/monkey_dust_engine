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

// ── FindByHash ────────────────────────────────────────────────────────────────

int RenderPassGraph::FindByHash(uint32_t h) const {
    for (int i = 0; i < count_; ++i)
        if (passes_[i].hash == h) return i;
    return -1;
}

// ── DeclareRead / DeclareWrite ────────────────────────────────────────────────

void RenderPassGraph::DeclareRead(const char* pass_name, const char* resource_name) {
    if (!pass_name || !resource_name) return;
    int idx = FindByHash(Hash(pass_name));
    if (idx < 0) return;  // unknown pass — silently ignore

    auto& e = passes_[idx];
    if (e.read_count >= RenderPassEntry::MAX_RESOURCE_BINDINGS) return;
    e.reads[e.read_count++] = { Hash(resource_name), RGAccess::Read };
}

void RenderPassGraph::DeclareWrite(const char* pass_name, const char* resource_name) {
    if (!pass_name || !resource_name) return;
    int idx = FindByHash(Hash(pass_name));
    if (idx < 0) return;

    auto& e = passes_[idx];
    if (e.write_count >= RenderPassEntry::MAX_RESOURCE_BINDINGS) return;
    e.writes[e.write_count++] = { Hash(resource_name), RGAccess::Write };
}

// ── Validate ──────────────────────────────────────────────────────────────────
// For each pass P that reads resource R:
//   Finds the last pass W that writes R (by registration index).
//   W must have index < P (W registered before P → executed before P).
//   Logs a warning if violated.

bool RenderPassGraph::Validate() const {
    bool ok = true;
    for (int pi = 0; pi < count_; ++pi) {
        const auto& P = passes_[pi];
        for (int ri = 0; ri < P.read_count; ++ri) {
            uint32_t res = P.reads[ri].resource_hash;
            // Find the latest writer of this resource.
            int writer_idx = -1;
            for (int wi = 0; wi < count_; ++wi) {
                const auto& W = passes_[wi];
                for (int k = 0; k < W.write_count; ++k) {
                    if (W.writes[k].resource_hash == res)
                        writer_idx = wi;  // take the latest
                }
            }
            if (writer_idx < 0) continue;  // no declared writer — assume external
            if (writer_idx >= pi) {
                fprintf(stderr,
                    "[RenderPassGraph] WARN: '%s' reads resource 0x%08x "
                    "but writer '%s' is registered AFTER it\n",
                    P.name, res, passes_[writer_idx].name);
                ok = false;
            }
        }
    }
    if (ok)
        fprintf(stdout, "[RenderPassGraph] Validate() OK — %d passes, no ordering violations\n",
                count_);
    return ok;
}

// ── Step 3.2: live resource handles ────────────────────────────────────────

int RenderPassGraph::FindResourceByHash(uint32_t h) const {
    for (int i = 0; i < resource_count_; ++i)
        if (resources_[i].hash == h) return i;
    return -1;
}

void RenderPassGraph::DeclareTextureDesc(const char* resource_name, const RGTextureDesc& desc) {
    if (!resource_name) return;
    uint32_t h = Hash(resource_name);
    int idx = FindResourceByHash(h);
    if (idx >= 0) { resources_[idx].desc = desc; return; }
    if (resource_count_ >= MAX_RESOURCES) {
        fprintf(stderr, "[RenderPassGraph] MAX_RESOURCES=%d reached, cannot declare '%s'\n",
                MAX_RESOURCES, resource_name);
        return;
    }
    auto& e = resources_[resource_count_++];
    e.hash     = h;
    e.desc     = desc;
    e.live_tex = nullptr;
}

SDL_GPUTexture* RenderPassGraph::ResolveTexture(md::GpuDeviceHandle dev, const char* resource_name) {
    if (!resource_name) return nullptr;
    int idx = FindResourceByHash(Hash(resource_name));
    if (idx < 0) {
        fprintf(stderr, "[RenderPassGraph] ResolveTexture: '%s' has no declared desc "
                "(call DeclareTextureDesc first)\n", resource_name);
        return nullptr;
    }
    auto& e = resources_[idx];
    if (e.live_tex) return e.live_tex;  // already resolved this frame
    e.live_tex = GpuTexturePool::Get().Acquire(dev, e.desc);
    return e.live_tex;
}

SDL_GPUTexture* RenderPassGraph::GetTexture(const char* resource_name) const {
    if (!resource_name) return nullptr;
    int idx = FindResourceByHash(Hash(resource_name));
    return (idx >= 0) ? resources_[idx].live_tex : nullptr;
}

void RenderPassGraph::ReleaseFrame() {
    for (int i = 0; i < resource_count_; ++i) {
        if (resources_[i].live_tex) {
            GpuTexturePool::Get().Release(resources_[i].live_tex);
            resources_[i].live_tex = nullptr;
        }
    }
}

} // namespace md
