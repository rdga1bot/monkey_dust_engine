#pragma once
#include "raylib.h"
#include <monkey_dust/platform/math_types.h>

static constexpr int MAX_INSTANCES = 1024;

// Батчинг статичних об'єктів через DrawMeshInstanced.
// Один draw call для сотень однакових мешів (дерева, каміння, руїни).
// Використання:
//   Instancer trees;
//   trees.Add(x, y, z, scale);   // при завантаженні чанку
//   trees.Draw(mesh, material);  // кожен кадр

class Instancer {
public:
    void Reset() { count_ = 0; }

    bool Add(float x, float y, float z, float scale = 1.0f) {
        if (count_ >= MAX_INSTANCES) return false;
        transforms_[count_] = mat4_mul(
            mat4_scale(scale, scale, scale),
            mat4_translate(x, y, z)
        );
        count_++;
        return true;
    }

    void Draw(Mesh mesh, Material mat) const {
        if (count_ == 0) return;
#ifndef USE_GLM
        DrawMeshInstanced(mesh, mat, transforms_, count_);
#else
        (void)mesh; (void)mat; // TODO M11.2: port instancer to MdMesh
#endif
    }

    int Count() const { return count_; }

private:
    Mat4 transforms_[MAX_INSTANCES];
    int  count_ = 0;
};
