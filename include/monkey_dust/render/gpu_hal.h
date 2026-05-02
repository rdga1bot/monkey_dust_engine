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

// ─────────────────────────────────────────────────────────────────────────────
// GpuComputePipeline — immutable compute shader program.
// SDL_GPU: SDL_CreateGPUComputePipeline (SPIR-V binary; see shaders/spirv/)
// ─────────────────────────────────────────────────────────────────────────────
class GpuComputePipeline {
public:
    bool Create (const char* glsl_path);   // compile + link compute shader
    void Destroy();
    int  UniformLoc(const char* name) const;

private:
    friend class GpuComputePass;
    unsigned int program_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuComputePass — scoped compute dispatch with explicit memory barrier.
//
// Usage:
//   pass.Begin(&pipeline);     // bind compute pipeline
//   // bind SSBOs / ring buffers before or here
//   pass.SetUniform*(...);     // optional uniforms
//   pass.Dispatch(gx, gy, gz); // one or more dispatches
//   pass.End(BARRIER_STORAGE); // glMemoryBarrier + unbind
//
// SDL_GPU:
//   Begin    → SDL_BeginGPUComputePass + SDL_BindGPUComputePipeline
//   Dispatch → SDL_DispatchGPUCompute
//   End      → SDL_EndGPUComputePass  (barriers implicit via resource declarations)
// ─────────────────────────────────────────────────────────────────────────────
class GpuComputePass {
public:
    // Barrier flags for End() — maps to GL_*_BARRIER_BIT constants.
    // SDL_GPU: expressed via SDL_GPUStorageBufferReadWriteBinding at Begin.
    static constexpr uint32_t BARRIER_STORAGE         = 1u; // GL_SHADER_STORAGE_BARRIER_BIT
    static constexpr uint32_t BARRIER_COMMAND         = 2u; // GL_COMMAND_BARRIER_BIT
    static constexpr uint32_t BARRIER_STORAGE_COMMAND = 3u; // both — for indirect draw output

    void Begin   (GpuComputePipeline* pipeline);

    // Uniform setters — SDL_GPU: SDL_PushGPUComputeUniformData(cmd, slot, data, size)
    void SetUniformFloat    (int loc, float v);
    void SetUniformInt      (int loc, int v);
    void SetUniformVec3     (int loc, const float* v3);
    void SetUniformVec4Array(int loc, const float* v4, int count);

    // SDL_GPU: SDL_DispatchGPUCompute
    void Dispatch(uint32_t gx, uint32_t gy, uint32_t gz);

    // glUseProgram(0) + glMemoryBarrier.
    // barrier_flags: BARRIER_STORAGE | BARRIER_COMMAND (or both).
    void End(uint32_t barrier_flags = BARRIER_STORAGE);

private:
    GpuComputePipeline* pipeline_ = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuStaticBuffer — immutable GPU buffer loaded once from CPU data.
// Use for static mesh geometry (position, normal, UV, index arrays).
// SDL_GPU: SDL_CreateGPUBuffer(usage=VERTEX|INDEX) + SDL_UploadToGPUBuffer
// ─────────────────────────────────────────────────────────────────────────────
class GpuStaticBuffer {
public:
    void Init(unsigned int gl_target,   // GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER
              const void* data,
              uint32_t    size_bytes);
    void Shutdown();

    // Bind for legacy-VAO style drawing (attrib pointer already stored in VAO).
    void Bind(unsigned int gl_target) const;

    // GL 4.3 DSA vertex binding (no current VAO needed).
    // SDL_GPU: slot corresponds to SDL_GPUVertexBufferDescription.slot
    void BindVertex(uint32_t slot, uint32_t stride, uint64_t offset = 0) const;

    unsigned int GLBuffer() const { return gl_buf_; }

    // Transfer ownership: returns GL ID and nulls this object (no double-delete).
    // Used by MdMesh::BuildMesh to place the buffer ID back into legacy struct fields.
    unsigned int Release() {
        unsigned int id = gl_buf_;
        gl_buf_ = 0;
        return id;
    }

private:
    unsigned int gl_buf_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuSamplerDesc — texture sampling parameters.
// SDL_GPU: SDL_GPUSamplerCreateInfo
// ─────────────────────────────────────────────────────────────────────────────
struct GpuSamplerDesc {
    enum class Filter : uint8_t { NEAREST, LINEAR, LINEAR_MIPMAP };
    enum class Wrap   : uint8_t { REPEAT, CLAMP_TO_EDGE };

    Filter min_filter = Filter::LINEAR_MIPMAP;
    Filter mag_filter = Filter::LINEAR;
    Wrap   wrap_s     = Wrap::REPEAT;
    Wrap   wrap_t     = Wrap::REPEAT;
    bool   gen_mipmap = false;
    bool   flip_v     = false;  // stbi flip (Flare atlas convention, DO NOT change)

    // Presets
    static GpuSamplerDesc Default() { return {}; }
    static GpuSamplerDesc PixelArt() {
        return { Filter::LINEAR_MIPMAP, Filter::NEAREST,
                 Wrap::CLAMP_TO_EDGE, Wrap::CLAMP_TO_EDGE, true, true };
    }
    static GpuSamplerDesc Lut() {
        return { Filter::LINEAR, Filter::LINEAR,
                 Wrap::CLAMP_TO_EDGE, Wrap::CLAMP_TO_EDGE, false, false };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuTexture — RGBA texture + sampler state.
// SDL_GPU: SDL_GPUTexture + SDL_CreateGPUSampler + SDL_UploadToGPUTexture
// ─────────────────────────────────────────────────────────────────────────────
class GpuTexture {
public:
    // Load from file (stb_image). Returns false on failure.
    bool InitFromFile  (const char* path,              const GpuSamplerDesc& s = {});
    // Upload raw RGBA8 pixel data.
    bool InitFromMemory(const uint8_t* rgba8, int w, int h, const GpuSamplerDesc& s = {});
    void Shutdown();

    // Bind to texture unit.
    // SDL_GPU: SDL_BindGPUFragmentSamplers(cmd, unit, &binding, 1)
    void Bind(uint32_t unit) const;

    int          Width()     const { return w_; }
    int          Height()    const { return h_; }
    unsigned int GLTexture() const { return id_; }
    bool         Valid()     const { return id_ != 0; }

    // Transfer ownership (same pattern as GpuStaticBuffer::Release).
    unsigned int Release() { unsigned int t = id_; id_ = 0; return t; }

private:
    void ApplySampler(const GpuSamplerDesc& s) const;
    unsigned int id_ = 0;
    int w_ = 0, h_ = 0;
};

#endif // MD_OPENGL43_ENABLED
