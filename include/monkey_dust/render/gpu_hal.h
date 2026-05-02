#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GPU Hardware Abstraction Layer — OpenGL 4.3 implementation.
//
// API design mirrors SDL_GPU so future backend swap is mechanical:
//
//   GpuPipeline       → SDL_CreateGPUGraphicsPipeline
//   GpuVertexBuffer   → SDL_GPUBuffer (device) + SDL_GPUTransferBuffer (staging)
//   GpuCommandBuffer  → SDL_GPUCommandBuffer + SDL_BeginGPURenderPass
//
// See CLAUDE_SDL_GPU_PREP.md for full migration notes.
// ─────────────────────────────────────────────────────────────────────────────

#include <monkey_dust/render/md_shader.h>
#include <monkey_dust/render/gpu_ring_buffer.h>
#include <cstdint>

#ifdef MD_OPENGL43_ENABLED

// ── Vertex topology ───────────────────────────────────────────────────────────
enum class GpuTopology : uint8_t { TRIANGLES, POINTS, LINES };

// ── Vertex attribute format ───────────────────────────────────────────────────
enum class GpuAttribFmt : uint8_t {
    F1, F2, F3, F4,       // float scalars / vectors
    U8x4_NORM             // uint8×4 normalized [0,1] → vec4
};

// ── Single vertex attribute descriptor ───────────────────────────────────────
struct GpuVertexAttrib {
    uint32_t    location;  // shader `layout(location = N) in`
    uint32_t    offset;    // byte offset within one vertex
    GpuAttribFmt fmt;
};

// ── Vertex buffer layout ──────────────────────────────────────────────────────
struct GpuVertexLayout {
    GpuVertexAttrib attribs[8] = {};
    uint32_t        count      = 0;
    uint32_t        stride     = 0;   // bytes per vertex
};

// ── Rasterizer / output-merger state (immutable in pipeline) ─────────────────
struct GpuRasterState {
    GpuTopology topology    = GpuTopology::TRIANGLES;
    bool  blend_enable      = false;
    uint32_t src_factor     = 0;   // GL_SRC_ALPHA etc.
    uint32_t dst_factor     = 0;
    bool  depth_test        = true;
    bool  depth_write       = true;
    bool  cull_back         = true;
    bool  point_size        = false; // GL_PROGRAM_POINT_SIZE
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuPipeline — immutable graphics pipeline.
// Create once at init; bind per draw call.
// SDL_GPU: SDL_CreateGPUGraphicsPipeline
// ─────────────────────────────────────────────────────────────────────────────
class GpuPipeline {
public:
    struct Desc {
        const char*    vert_path = nullptr;
        const char*    frag_path = nullptr;
        GpuVertexLayout layout;
        GpuRasterState  raster;
    };

    bool Create(const Desc& desc);
    void Destroy();

    // Return the GL uniform location for a named uniform.
    // SDL_GPU replacement: named locations are replaced by push_constant offsets.
    int UniformLoc(const char* name) const;

private:
    friend class GpuCommandBuffer;
    unsigned int vao_    = 0;
    MdShader     shader_ = {};
    GpuRasterState raster_ = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuVertexBuffer — per-frame vertex data with ring buffering.
// SDL_GPU: SDL_GPUBuffer (device-side) + SDL_GPUTransferBuffer (CPU staging).
// ─────────────────────────────────────────────────────────────────────────────
class GpuVertexBuffer {
public:
    void Init(uint32_t max_vertices, uint32_t vertex_stride);
    void Shutdown();

    // Get CPU-writable pointer for this frame's vertex data.
    // SDL_GPU: SDL_MapGPUTransferBuffer
    void* MapWrite();

    // Flush (no-op for coherent GL mapping; SDL_UnmapGPUTransferBuffer).
    void  Unmap();

    // Insert fence + rotate to next ring slot.
    // Call AFTER all draws that read this buffer.
    // SDL_GPU: SDL_UploadToGPUBuffer(cycle=true)
    void  Advance();

    uint32_t Stride() const { return stride_; }

private:
    friend class GpuCommandBuffer;
    GpuRingBuffer ring_;
    uint32_t      stride_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuCommandBuffer — records (and immediately executes) one render pass.
// SDL_GPU: SDL_GPUCommandBuffer + SDL_BeginGPURenderPass
// ─────────────────────────────────────────────────────────────────────────────
class GpuCommandBuffer {
public:
    // Bind pipeline: apply shader + raster state.
    // SDL_GPU: SDL_BindGPUGraphicsPipeline
    void BindPipeline(GpuPipeline* pipeline);

    // Bind vertex data source.
    // SDL_GPU: SDL_BindGPUVertexBuffers
    void BindVertexBuffer(GpuVertexBuffer* buf);

    // Upload small per-draw constants via named uniform location.
    // SDL_GPU: SDL_PushGPUVertexUniformData(cmd, slot, data, size)
    void SetUniformMat4(int loc, const float* m16);
    void SetUniformVec3(int loc, const float* v3);

    // Issue draw call.
    // SDL_GPU: SDL_DrawGPUPrimitives
    void Draw(uint32_t vertex_count, uint32_t first_vertex = 0);

    // Restore GL state changed by BindPipeline.
    // SDL_GPU: SDL_EndGPURenderPass (state is scope-managed)
    void EndPass();

private:
    GpuPipeline* pipeline_ = nullptr;
};

#endif // MD_OPENGL43_ENABLED
