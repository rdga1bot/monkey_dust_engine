// PropMesh — GLB loader using cgltf (CGLTF_IMPLEMENTATION in cgltf_impl.cpp).
// Only the first mesh primitive is loaded; positions + normals are extracted.
#include <monkey_dust/render/prop_mesh.h>

// cgltf.h lives in the same directory as this .cpp.
// CGLTF_IMPLEMENTATION is provided by cgltf_impl.cpp (compiled separately).
#include "cgltf.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

bool PropMesh::LoadGLB(const char* path) {
    loaded = false;
    if (!path) return false;

    cgltf_options opts = {};
    cgltf_data*   data = nullptr;

    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success) {
        fprintf(stderr, "[PropMesh] cgltf_parse_file failed: %s\n", path);
        return false;
    }

    if (cgltf_load_buffers(&opts, data, path) != cgltf_result_success) {
        fprintf(stderr, "[PropMesh] cgltf_load_buffers failed: %s\n", path);
        cgltf_free(data);
        return false;
    }

    // Locate first mesh primitive with POSITION + NORMAL.
    cgltf_primitive* prim = nullptr;
    for (cgltf_size mi = 0; mi < data->meshes_count && !prim; ++mi) {
        for (cgltf_size pi = 0; pi < data->meshes[mi].primitives_count && !prim; ++pi) {
            cgltf_primitive* p = &data->meshes[mi].primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            bool has_pos = false, has_norm = false;
            for (cgltf_size ai = 0; ai < p->attributes_count; ++ai) {
                if (p->attributes[ai].type == cgltf_attribute_type_position) has_pos  = true;
                if (p->attributes[ai].type == cgltf_attribute_type_normal)   has_norm = true;
            }
            if (has_pos && has_norm && p->indices) prim = p;
        }
    }

    if (!prim) {
        fprintf(stderr, "[PropMesh] No valid primitive with POSITION+NORMAL+indices: %s\n", path);
        cgltf_free(data);
        return false;
    }

    // Find POSITION and NORMAL accessors.
    cgltf_accessor* pos_acc  = nullptr;
    cgltf_accessor* norm_acc = nullptr;
    for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
        if (prim->attributes[ai].type == cgltf_attribute_type_position) pos_acc  = prim->attributes[ai].data;
        if (prim->attributes[ai].type == cgltf_attribute_type_normal)   norm_acc = prim->attributes[ai].data;
    }

    cgltf_size vert_count = pos_acc->count;
    if (vert_count == 0 || vert_count > 131072) {
        fprintf(stderr, "[PropMesh] Unexpected vertex count %zu: %s\n", vert_count, path);
        cgltf_free(data);
        return false;
    }

    // Build interleaved VBO: PropVertex { pos(12) + norm(12) }.
    static PropVertex s_verts[131072];
    for (cgltf_size i = 0; i < vert_count; ++i) {
        float p[3] = {0.f, 0.f, 0.f};
        float n[3] = {0.f, 1.f, 0.f};
        cgltf_accessor_read_float(pos_acc,  i, p, 3);
        cgltf_accessor_read_float(norm_acc, i, n, 3);
        s_verts[i] = { p[0], p[1], p[2], n[0], n[1], n[2] };
    }

    vbo.Init(0x8892u /*GL_ARRAY_BUFFER*/,
             s_verts,
             (uint32_t)(vert_count * sizeof(PropVertex)));

    // Build IBO: uint16_t if vert_count <= 65535, else uint32_t.
    cgltf_accessor* idx_acc = prim->indices;
    cgltf_size      idx_count = idx_acc->count;

    indices_u16 = (vert_count <= 65535u);
    index_count = (uint32_t)idx_count;

    if (indices_u16) {
        static uint16_t s_idx16[1024 * 1024];
        for (cgltf_size i = 0; i < idx_count; ++i)
            s_idx16[i] = (uint16_t)cgltf_accessor_read_index(idx_acc, i);
        ibo.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/,
                 s_idx16,
                 (uint32_t)(idx_count * sizeof(uint16_t)));
    } else {
        static uint32_t s_idx32[1024 * 1024];
        for (cgltf_size i = 0; i < idx_count; ++i)
            s_idx32[i] = (uint32_t)cgltf_accessor_read_index(idx_acc, i);
        ibo.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/,
                 s_idx32,
                 (uint32_t)(idx_count * sizeof(uint32_t)));
    }

    cgltf_free(data);
    loaded = true;
    fprintf(stdout, "[PropMesh] Loaded %zu verts / %zu idx (u%s): %s\n",
            vert_count, idx_count, indices_u16 ? "16" : "32", path);
    return true;
}

void PropMesh::Shutdown() {
    vbo.Shutdown();
    ibo.Shutdown();
    index_count = 0;
    loaded      = false;
}
