#pragma once
#include "raylib.h"
#include "raymath.h"

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
        transforms_[count_] = MatrixMultiply(
            MatrixScale(scale, scale, scale),
            MatrixTranslate(x, y, z)
        );
        count_++;
        return true;
    }

    void Draw(Mesh mesh, Material mat) const {
        if (count_ == 0) return;
        DrawMeshInstanced(mesh, mat, transforms_, count_);
    }

    int Count() const { return count_; }

private:
    Matrix transforms_[MAX_INSTANCES];
    int    count_ = 0;
};
