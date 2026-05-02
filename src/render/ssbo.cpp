#include <monkey_dust/render/ssbo.h>

#ifdef MD_OPENGL43_ENABLED
#include <cstdio>
#include "glad.h"

void SSBO::Init(int capacity_bytes) {
    glGenBuffers(1, &id);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, capacity_bytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    if (!id) fprintf(stderr, "[SSBO] creation failed (%d bytes)\n", capacity_bytes);
}
void SSBO::Upload(const void* data, int size_bytes, int offset) {
    if (!id) return;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size_bytes, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
void SSBO::Bind(int binding_point) {
    if (id) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)binding_point, id);
}
void SSBO::Shutdown() {
    if (id) { glDeleteBuffers(1, &id); id = 0; }
}

#else
void SSBO::Init(int) {}
void SSBO::Upload(const void*, int, int) {}
void SSBO::Bind(int) {}
void SSBO::Shutdown() {}
#endif
