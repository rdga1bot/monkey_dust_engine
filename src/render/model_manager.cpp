#include <monkey_dust/render/model_manager.h>

#ifdef MD_OPENGL43_ENABLED
// cgltf declarations only (no CGLTF_IMPLEMENTATION).
// Implementation is provided by Raylib's rmodels.c when linking libraylib.a.
// For a no-Raylib build, add a separate cgltf_impl.cpp with #define CGLTF_IMPLEMENTATION.
#include "cgltf.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Extract parent directory of a file path (including trailing slash).
static void PathDir(const char* path, char* out, int maxlen) {
    strncpy(out, path, maxlen - 1);
    out[maxlen - 1] = '\0';
    char* last = nullptr;
    for (char* p = out; *p; ++p)
        if (*p == '/' || *p == '\\') last = p;
    if (last) *(last + 1) = '\0';
    else      out[0] = '\0';
}

bool ModelManager::Load(const char* name, const char* path, MdMesh fallback_mesh) {
    if (count_ >= MAX_MODELS) {
        fprintf(stderr, "[ModelManager] MAX_MODELS reached, cannot load '%s'\n", name);
        MdUnloadMesh(fallback_mesh);
        return false;
    }
    Entry& e = entries_[count_];
    strncpy(e.name, name, 31);
    e.name[31] = '\0';

    cgltf_options opts = {};
    cgltf_data*   data = nullptr;

    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success) {
        fprintf(stderr, "[ModelManager] '%s' cgltf failed, using fallback\n", path);
        e.model.mesh  = fallback_mesh;
        e.model.valid = true;
        count_++;
        return true;
    }
    cgltf_load_buffers(&opts, data, path);

    bool mesh_loaded = false;

    if (data->meshes_count > 0 && data->meshes[0].primitives_count > 0) {
        cgltf_primitive* prim = &data->meshes[0].primitives[0];

        // Count vertices from POSITION accessor
        int vcount = 0;
        for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
            if (prim->attributes[a].type == cgltf_attribute_type_position) {
                vcount = (int)prim->attributes[a].data->count;
                break;
            }
        }
        int icount = prim->indices ? (int)prim->indices->count : 0;

        if (vcount > 0 && icount > 0) {
            float*        pos  = (float*)malloc((size_t)vcount * 3 * sizeof(float));
            float*        norm = (float*)calloc((size_t)vcount * 3, sizeof(float));
            float*        uv   = (float*)calloc((size_t)vcount * 2, sizeof(float));
            unsigned int* idx  = (unsigned int*)malloc((size_t)icount * sizeof(unsigned int));

            // Read vertex attributes
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_position) {
                    for (int i = 0; i < vcount; ++i)
                        cgltf_accessor_read_float(attr->data, (cgltf_size)i, pos + i*3, 3);
                } else if (attr->type == cgltf_attribute_type_normal) {
                    for (int i = 0; i < vcount; ++i)
                        cgltf_accessor_read_float(attr->data, (cgltf_size)i, norm + i*3, 3);
                } else if (attr->type == cgltf_attribute_type_texcoord
                           && attr->index == 0) {
                    for (int i = 0; i < vcount; ++i)
                        cgltf_accessor_read_float(attr->data, (cgltf_size)i, uv + i*2, 2);
                }
            }

            // Read indices
            for (int i = 0; i < icount; ++i) {
                cgltf_uint v = 0;
                cgltf_accessor_read_uint(prim->indices, (cgltf_size)i, &v, 1);
                idx[i] = (unsigned int)v;
            }

            e.model.mesh  = MdMeshFromBuffers(pos, norm, uv, idx, vcount, icount);
            mesh_loaded   = true;
            free(pos); free(norm); free(uv); free(idx);

            // Try to load albedo texture
            if (prim->material && prim->material->has_pbr_metallic_roughness) {
                cgltf_texture* tex =
                    prim->material->pbr_metallic_roughness.base_color_texture.texture;
                if (tex && tex->image && tex->image->uri) {
                    char dir[256], tex_path[512];
                    PathDir(path, dir, sizeof(dir));
                    snprintf(tex_path, sizeof(tex_path), "%s%s", dir, tex->image->uri);
                    e.model.albedo = MdLoadTexture(tex_path);
                }
            }
        }
    }

    cgltf_free(data);

    if (!mesh_loaded) {
        fprintf(stderr, "[ModelManager] '%s' no mesh data, using fallback\n", path);
        e.model.mesh = fallback_mesh;
    } else {
        MdUnloadMesh(fallback_mesh);
    }

    e.model.valid = true;
    count_++;
    return true;
}

MdModel& ModelManager::Get(const char* name) {
    for (int i = 0; i < count_; ++i)
        if (strncmp(entries_[i].name, name, 31) == 0)
            return entries_[i].model;
    fprintf(stderr, "[ModelManager] Model '%s' not found\n", name);
    return entries_[0].model;
}

void ModelManager::Unload() {
    for (int i = 0; i < count_; ++i) {
        MdUnloadMesh   (entries_[i].model.mesh);
        MdUnloadTexture(entries_[i].model.albedo);
        MdUnloadTexture(entries_[i].model.metallic_rough);
    }
    count_ = 0;
}
#endif // MD_OPENGL43_ENABLED
