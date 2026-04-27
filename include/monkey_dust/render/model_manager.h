#pragma once
#include <cstring>

static constexpr int MAX_MODELS = 32;

// ── USE_SDL3 path: MdModel via cgltf ────────────────────────────────────────
#ifdef USE_SDL3
#include <monkey_dust/render/md_mesh.h>
#include <monkey_dust/render/md_texture.h>

struct MdModel {
    MdMesh    mesh;
    MdTexture albedo;
    MdTexture metallic_rough;
    bool      valid = false;
};

class ModelManager {
public:
    static ModelManager& Get() {
        static ModelManager inst;
        return inst;
    }

    // Load GLTF model from path; on failure falls back to fallback_mesh.
    // Implementation in ModelManager.cpp (cgltf, no CGLTF_IMPLEMENTATION here).
    bool     Load(const char* name, const char* path, MdMesh fallback_mesh);
    MdModel& Get(const char* name);
    void     Unload();

private:
    ModelManager() = default;

    struct Entry {
        char    name[32] = {};
        MdModel model;
    };

    Entry entries_[MAX_MODELS];
    int   count_ = 0;
};

// ── !USE_SDL3 path: Raylib Model ─────────────────────────────────────────────
#else
#include "raylib.h"

class ModelManager {
public:
    static ModelManager& Get() {
        static ModelManager inst;
        return inst;
    }

    bool Load(const char* name, const char* path, Mesh fallback_mesh) {
        if (count_ >= MAX_MODELS) {
            TraceLog(LOG_WARNING, "[ModelManager] MAX_MODELS reached, cannot load '%s'", name);
            UnloadMesh(fallback_mesh);
            return false;
        }
        Entry& e = entries_[count_];
        strncpy(e.name, name, 31);
        e.name[31] = '\0';
        e.model = LoadModel(path);
        if (!IsModelValid(e.model)) {
            e.model = LoadModelFromMesh(fallback_mesh);
            TraceLog(LOG_INFO, "[ModelManager] '%s' fallback: %s not found", name, path);
        } else {
            UnloadMesh(fallback_mesh);
        }
        count_++;
        return true;
    }

    Model& Get(const char* name) {
        for (int i = 0; i < count_; ++i)
            if (strncmp(entries_[i].name, name, 31) == 0)
                return entries_[i].model;
        TraceLog(LOG_WARNING, "[ModelManager] Model '%s' not found", name);
        return entries_[0].model;
    }

    void Unload() {
        for (int i = 0; i < count_; ++i)
            UnloadModel(entries_[i].model);
        count_ = 0;
    }

private:
    ModelManager() = default;

    struct Entry {
        char  name[32];
        Model model;
    };

    Entry entries_[MAX_MODELS];
    int   count_ = 0;
};
#endif
