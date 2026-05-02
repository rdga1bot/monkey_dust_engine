#include <monkey_dust/render/compute_shader.h>

#ifdef MD_OPENGL43_ENABLED
#include <cstdio>
#include <cstdlib>
#include "glad.h"

static char* LoadSourceFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return nullptr; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

bool ComputeShader::Load(const char* path) {
    char* src = LoadSourceFile(path);
    if (!src) { fprintf(stderr, "[ComputeShader] file not found: %s\n", path); return false; }

    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* srcp = src;
    glShaderSource(shader, 1, &srcp, nullptr);
    glCompileShader(shader);
    free(src);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(shader, 512, nullptr, log);
        fprintf(stderr, "[ComputeShader] compile error %s: %s\n", path, log);
        glDeleteShader(shader);
        return false;
    }

    program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);

    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(program, 512, nullptr, log);
        fprintf(stderr, "[ComputeShader] link error %s: %s\n", path, log);
        glDeleteProgram(program);
        program = 0;
        return false;
    }
    return true;
}

void ComputeShader::Dispatch(unsigned int gx, unsigned int gy, unsigned int gz) {
    glDispatchCompute(gx, gy, gz);
}

void ComputeShader::Enable()  { glUseProgram(program); }
void ComputeShader::Disable() { glUseProgram(0); }

void ComputeShader::Unload() {
    if (program) { glDeleteProgram(program); program = 0; }
}

int ComputeShader::GetUniformLoc(const char* name) const {
    return glGetUniformLocation(program, name);
}
#endif
