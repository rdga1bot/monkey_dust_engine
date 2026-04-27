#pragma once
#include <cstdint>

// Thin wrapper around Compute Shader lifecycle.
// All rlgl/OpenGL calls are confined here — the only other allowed file is SSBO.h.
// Migration M2: replace body with glad glCreateShader/glCompileShader.
class ComputeShader {
public:
    unsigned int program = 0;

#ifdef MD_OPENGL43_ENABLED
    // Returns false on compile failure (logged to stderr).
    bool Load(const char* path);
    void Dispatch(unsigned int groups_x, unsigned int groups_y, unsigned int groups_z);
    void Enable();
    void Disable();
    void Unload();

    int GetUniformLoc(const char* name) const;
#else
    bool Load(const char*) { return false; }
    void Dispatch(unsigned int, unsigned int, unsigned int) {}
    void Enable()  {}
    void Disable() {}
    void Unload()  {}
    int  GetUniformLoc(const char*) const { return -1; }
#endif
};
