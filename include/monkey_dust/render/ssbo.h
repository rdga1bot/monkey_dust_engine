#pragma once
#include <cstdint>

// GPU Shader Storage Buffer Object wrapper.
// All methods are no-ops when MD_OPENGL43_ENABLED is not defined.
class SSBO {
public:
    unsigned int id = 0;
    void Init(int capacity_bytes);
    void Upload(const void* data, int size_bytes, int offset = 0);
    void Bind(int binding_point);
    void Shutdown();
};
