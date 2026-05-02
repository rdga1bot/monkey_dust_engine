#pragma once
#include <monkey_dust/platform/math_types.h>
#include <monkey_dust/render/md_mesh.h>

#ifdef MD_OPENGL43_ENABLED
#include "glad.h"
#endif

static constexpr int MAX_INSTANCES = 1024;

// Batch static objects via GL instanced draw.
// One draw call for hundreds of identical meshes (trees, rocks, ruins).
// Usage:
//   Instancer trees;
//   trees.Add(x, y, z, scale);                   // at chunk load
//   glUseProgram(shader.id);
//   trees.Draw(mesh, loc_vp, mat4_ptr(vp));       // each frame
//   glUseProgram(0);
//   trees.Shutdown();                             // at cleanup

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

#ifdef MD_OPENGL43_ENABLED
    // Draw all instances. Caller must have the shader active (glUseProgram).
    // loc_vp: glGetUniformLocation(shader, "viewProj")
    // vp16:   mat4_ptr(viewProjMatrix)
    void Draw(MdMesh mesh, int loc_vp, const float* vp16) {
        if (!mesh.vao || count_ == 0) return;

        // Lazy-init instance VBO and wire attribs 3-6 on the mesh's VAO.
        if (!inst_vbo_) {
            glGenBuffers(1, &inst_vbo_);
            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
            glBufferData(GL_ARRAY_BUFFER, MAX_INSTANCES * 64, nullptr, GL_DYNAMIC_DRAW);
            for (int i = 0; i < 4; ++i) {
                glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE,
                                      64, (void*)(intptr_t)(i * 16));
                glEnableVertexAttribArray(3 + i);
                glVertexAttribDivisor(3 + i, 1);
            }
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        glBindBuffer(GL_ARRAY_BUFFER, inst_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(count_ * 64), transforms_);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glUniformMatrix4fv(loc_vp, 1, GL_FALSE, vp16);
        glBindVertexArray(mesh.vao);
        glDrawElementsInstanced(GL_TRIANGLES, mesh.index_count,
                                GL_UNSIGNED_INT, nullptr, count_);
        glBindVertexArray(0);
    }

    void Shutdown() {
        if (inst_vbo_) { glDeleteBuffers(1, &inst_vbo_); inst_vbo_ = 0; }
    }
#endif

    int Count() const { return count_; }

private:
    Mat4         transforms_[MAX_INSTANCES];
    int          count_    = 0;
#ifdef MD_OPENGL43_ENABLED
    unsigned int inst_vbo_ = 0;
#endif
};
