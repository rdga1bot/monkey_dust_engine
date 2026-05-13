#include <monkey_dust/render/md_shader.h>

#include "glad.h"
#include <cstdio>
#include <cstdlib>

static char* MdReadFile(const char* path) {
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

static GLuint CompileStage(const char* path, GLenum type) {
    char* src = MdReadFile(path);
    if (!src) {
        fprintf(stderr, "[MdShader] file not found: %s\n", path);
        return 0;
    }
    GLuint sh = glCreateShader(type);
    const char* p = src;
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    free(src);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, 512, nullptr, log);
        fprintf(stderr, "[MdShader] compile error %s: %s\n", path, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

MdShader MdLoadShader(const char* vert_path, const char* frag_path) {
    GLuint vs = CompileStage(vert_path, GL_VERTEX_SHADER);
    GLuint fs = CompileStage(frag_path, GL_FRAGMENT_SHADER);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return {};
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        fprintf(stderr, "[MdShader] link error (%s + %s): %s\n", vert_path, frag_path, log);
        glDeleteProgram(prog);
        return {};
    }
    MdShader s; s.id = prog;
    return s;
}

void MdUnloadShader(MdShader& s) {
    if (s.id) { glDeleteProgram(s.id); s.id = 0; }
}

int  MdGetLoc(MdShader s, const char* name) {
    return glGetUniformLocation(s.id, name);
}

void MdSetFloat   (int loc, float v)           { if (loc >= 0) glUniform1f (loc, v); }
void MdSetInt     (int loc, int v)             { if (loc >= 0) glUniform1i (loc, v); }
void MdSetVec3    (int loc, const float* v)    { if (loc >= 0) glUniform3fv(loc, 1, v); }
void MdSetVec4    (int loc, const float* v)    { if (loc >= 0) glUniform4fv(loc, 1, v); }
void MdSetMat4    (int loc, const float* m)    { if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m); }
void MdSetFloatArr(int loc, const float* v, int n) { if (loc >= 0) glUniform1fv(loc, n, v); }
void MdSetVec4Arr (int loc, const float* v, int n) { if (loc >= 0) glUniform4fv(loc, n, v); }

void MdUseShader (MdShader s) { glUseProgram(s.id); }
void MdStopShader()           { glUseProgram(0); }
