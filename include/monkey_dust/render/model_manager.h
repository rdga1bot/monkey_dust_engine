#pragma once
#include <monkey_dust/render/md_mesh.h>
#include <monkey_dust/render/md_texture.h>
#include <cstring>

static constexpr int MAX_MODELS = 32;

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
    // Implementation in model_manager.cpp (cgltf, no CGLTF_IMPLEMENTATION here).
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
