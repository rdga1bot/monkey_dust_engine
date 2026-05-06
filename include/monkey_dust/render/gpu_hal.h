#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GPU Hardware Abstraction Layer — dual-backend (OpenGL 4.3 / SDL_GPU).
//
// API design mirrors SDL_GPU so future backend swap is mechanical:
//
//   GpuPipeline       → SDL_CreateGPUGraphicsPipeline
//   GpuVertexBuffer   → SDL_GPUBuffer (device) + SDL_GPUTransferBuffer (staging)
//   GpuCommandBuffer  → SDL_GPUCommandBuffer + SDL_BeginGPURenderPass
//
// Backend selection:
//   OpenGL 4.3 path:  compiled when MD_OPENGL43_ENABLED is defined
//   SDL_GPU path:     compiled when MD_SDL_GPU       is defined
//   Both can be active simultaneously during migration (dual-backend).
// ─────────────────────────────────────────────────────────────────────────────

#if defined(MD_OPENGL43_ENABLED) || defined(MD_SDL_GPU)

#include <monkey_dust/render/md_shader.h>
#include <monkey_dust/render/gpu_ring_buffer.h>
#include <cstdint>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

// ── Vertex topology ───────────────────────────────────────────────────────────
enum class GpuTopology : uint8_t { TRIANGLES, POINTS, LINES };

// ── Vertex attribute format ───────────────────────────────────────────────────
enum class GpuAttribFmt : uint8_t {
    F1, F2, F3, F4,       // float scalars / vectors
    U8x4_NORM             // uint8×4 normalized [0,1] → vec4
};

// ── Blend factor (portable; maps to GL and SDL_GPU) ───────────────────────────
enum class GpuBlendFactor : uint8_t {
    ZERO,
    ONE,
    SRC_COLOR,
    ONE_MINUS_SRC_COLOR,
    SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA,
    DST_ALPHA,
    ONE_MINUS_DST_ALPHA,
    DST_COLOR,
    ONE_MINUS_DST_COLOR,
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
    GpuTopology   topology    = GpuTopology::TRIANGLES;
    bool          blend_enable = false;
    GpuBlendFactor src_factor  = GpuBlendFactor::SRC_ALPHA;
    GpuBlendFactor dst_factor  = GpuBlendFactor::ONE_MINUS_SRC_ALPHA;
    bool  depth_test           = true;
    bool  depth_write          = true;
    bool  cull_back            = true;
    bool  point_size           = false; // GL_PROGRAM_POINT_SIZE
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuPipeline — immutable graphics pipeline.
// Create once at init; bind per draw call.
// SDL_GPU: SDL_CreateGPUGraphicsPipeline
// ─────────────────────────────────────────────────────────────────────────────
class GpuPipeline {
public:
    struct Desc {
        const char*     vert_path = nullptr;  // GLSL (OpenGL) or SPIR-V basename (SDL_GPU)
        const char*     frag_path = nullptr;
        GpuVertexLayout layout;
        GpuRasterState  raster;
#ifdef MD_SDL_GPU
        // Shader resource counts required by SDL_GPUShaderCreateInfo.
        // Must match the SPIR-V descriptor declarations exactly.
        uint32_t vert_uniform_bufs = 0;
        uint32_t vert_storage_bufs = 0;
        uint32_t frag_uniform_bufs = 0;
        uint32_t frag_storage_bufs = 0;
        uint32_t frag_samplers     = 0;
        bool     has_depth_target  = true;  // set false for 2D/HUD passes
#endif
    };

    bool Create(const Desc& desc);
    void Destroy();

#ifdef MD_OPENGL43_ENABLED
    // Return the GL uniform location for a named uniform.
    // SDL_GPU replacement: use push_constant offsets / UBO members.
    int UniformLoc(const char* name) const;
#endif

private:
    friend class GpuCommandBuffer;
#ifdef MD_OPENGL43_ENABLED
    unsigned int vao_    = 0;
    MdShader     shader_ = {};
#endif
#ifdef MD_SDL_GPU
    SDL_GPUGraphicsPipeline* sdl_pipeline_ = nullptr;
#endif
    GpuRasterState raster_ = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuVertexBuffer — per-frame vertex data with ring buffering.
// OpenGL: GpuRingBuffer (GL 4.4 persistent coherent mapping, N=3 fence slots).
// SDL_GPU: SDL_GPUTransferBuffer (CPU staging, cycle=true) +
//          SDL_GPUBuffer (device VERTEX, cycle=true upload).
// ─────────────────────────────────────────────────────────────────────────────
class GpuVertexBuffer {
public:
    void Init(uint32_t max_vertices, uint32_t vertex_stride);
    void Shutdown();

    // Get CPU-writable pointer for this frame's vertex data.
    // SDL_GPU: SDL_MapGPUTransferBuffer(cycle=true)
    void* MapWrite();

    // Flush — no-op for coherent GL mapping; SDL_UnmapGPUTransferBuffer.
    void  Unmap();

    // SDL_GPU: copy transfer → device buffer via copy pass inside cmd.
    // Must be called before the render pass that reads this buffer.
    // OpenGL: no-op (data already coherently visible).
#ifdef MD_SDL_GPU
    void Upload(SDL_GPUCommandBuffer* cmd);
    SDL_GPUBuffer* SDLBuffer() const { return sdl_buf_; }
#endif

    // Insert fence + rotate ring slot (OpenGL). No-op in SDL_GPU path.
    void Advance();

    uint32_t Stride() const { return stride_; }

private:
    friend class GpuCommandBuffer;
    GpuRingBuffer ring_;       // OpenGL path (no-op stub when !MD_OPENGL43_ENABLED)
    uint32_t      stride_ = 0;
#ifdef MD_SDL_GPU
    SDL_GPUBuffer*         sdl_buf_      = nullptr;
    SDL_GPUTransferBuffer* sdl_transfer_ = nullptr;
    uint32_t               sdl_size_     = 0;
#endif
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
    // SDL_GPU: no-op — use PushVertexUniforms / PushFragmentUniforms instead.
    void SetUniformMat4(int loc, const float* m16);
    void SetUniformVec3(int loc, const float* v3);

    // Issue draw call.
    // SDL_GPU: SDL_DrawGPUPrimitives
    void Draw(uint32_t vertex_count, uint32_t first_vertex = 0);

    // Restore GL state / end SDL_GPU render pass.
    void EndPass();

#ifdef MD_SDL_GPU
    // Open a color (+ optional depth) render pass on an existing command buffer.
    struct ColorPassDesc {
        SDL_GPUCommandBuffer* cmd;
        SDL_GPUTexture*       color_tex;
        SDL_GPUTexture*       depth_tex      = nullptr;
        float                 clear_color[4] = {0.f, 0.f, 0.f, 1.f};
        float                 clear_depth    = 1.0f;
        bool                  load_color     = false; // false = CLEAR on entry
        bool                  load_depth     = false;
    };
    void BeginColorPass(const ColorPassDesc& desc);

    // Bind textures + samplers for the fragment stage.
    void BindFragmentSamplers(uint32_t first_slot,
                               const SDL_GPUTextureSamplerBinding* bindings,
                               uint32_t count);

    // Push small uniform structs (UBO slot 0/1 mapped via SPIRV_COMPILE bindings).
    void PushVertexUniforms  (uint32_t slot, const void* data, uint32_t size_bytes);
    void PushFragmentUniforms(uint32_t slot, const void* data, uint32_t size_bytes);

    SDL_GPURenderPass*    SDLPass() const { return sdl_pass_; }
    SDL_GPUCommandBuffer* SDLCmd()  const { return sdl_cmd_; }
#endif

private:
    GpuPipeline* pipeline_ = nullptr;
#ifdef MD_SDL_GPU
    SDL_GPUCommandBuffer* sdl_cmd_  = nullptr;
    SDL_GPURenderPass*    sdl_pass_ = nullptr;
#endif
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
    static constexpr uint32_t BARRIER_STORAGE         = 1u; // GL_SHADER_STORAGE_BARRIER_BIT
    static constexpr uint32_t BARRIER_COMMAND         = 2u; // GL_COMMAND_BARRIER_BIT
    static constexpr uint32_t BARRIER_STORAGE_COMMAND = 3u; // both — for indirect draw output

    void Begin   (GpuComputePipeline* pipeline);
    void SetUniformFloat    (int loc, float v);
    void SetUniformInt      (int loc, int v);
    void SetUniformVec3     (int loc, const float* v3);
    void SetUniformVec4Array(int loc, const float* v4, int count);
    void Dispatch(uint32_t gx, uint32_t gy, uint32_t gz);
    void End(uint32_t barrier_flags = BARRIER_STORAGE);

private:
    GpuComputePipeline* pipeline_ = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuDepthTexture — depth texture for shadow passes.
// OpenGL: FBO + GL_DEPTH_COMPONENT24 texture.
// SDL_GPU: SDL_GPUTexture(D24_UNORM, DEPTH_STENCIL_TARGET|SAMPLER) — no FBO.
//   shadow_border: GL uses CLAMP_TO_BORDER (white); SDL_GPU uses CLAMP_TO_EDGE
//   (SDL3 has no border color support — minor edge artifact, acceptable).
// ─────────────────────────────────────────────────────────────────────────────
class GpuDepthTexture {
public:
    void Init(int w, int h, bool shadow_border = false);
    void Shutdown();

    // OpenGL: glActiveTexture + glBindTexture.
    // SDL_GPU: binding done via SDL_BindGPUFragmentSamplers in render pass (Step 6).
    void Bind(uint32_t unit) const;

    unsigned int FBO()     const { return fbo_; }   // 0 in SDL_GPU path
    unsigned int Texture() const { return tex_; }   // 0 in SDL_GPU path
    int Width()  const { return w_; }
    int Height() const { return h_; }
#ifdef MD_SDL_GPU
    SDL_GPUTexture*  SDLTexture() const { return sdl_tex_; }
    SDL_GPUSampler*  SDLSampler() const { return sdl_sampler_; }
#endif

private:
    unsigned int fbo_ = 0;
    unsigned int tex_ = 0;
    int w_ = 0, h_ = 0;
#ifdef MD_SDL_GPU
    SDL_GPUTexture* sdl_tex_     = nullptr;
    SDL_GPUSampler* sdl_sampler_ = nullptr;
#endif
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuRenderPass — scoped render pass bound to one or more attachments.
// SDL_GPU: SDL_BeginGPURenderPass / SDL_EndGPURenderPass
// ─────────────────────────────────────────────────────────────────────────────
class GpuRenderPass {
public:
    struct DepthDesc {
        GpuDepthTexture* target;
        float clear_depth = 1.0f;
        bool  cull_front  = false; // GL_FRONT for shadow bias (Peter-Panning fix)
    };

    // Shadow / depth-only pass.
    // OpenGL: FBO bind + clear.
    void BeginDepthOnly(const DepthDesc& desc);

#ifdef MD_SDL_GPU
    // SDL_GPU depth-only variant: requires the frame command buffer.
    void BeginDepthOnly(SDL_GPUCommandBuffer* cmd, const DepthDesc& desc);
#endif

    // ── Color pass (main render target) ──────────────────────────────────────
    struct ColorDesc {
        float            clear[4]    = {0.f, 0.f, 0.f, 1.f};
        float            clear_depth = 1.0f;
        GpuDepthTexture* depth       = nullptr; // optional depth attachment
#ifdef MD_SDL_GPU
        // SDL_GPU: frame command buffer acquired from GpuDevice::AcquireCommandBuffer().
        SDL_GPUCommandBuffer* cmd = nullptr;
#endif
    };

    // SDL_GPU: AcquireSwapchainTexture + SDL_BeginGPURenderPass(color+depth).
    // OpenGL: bind FBO 0, save viewport, glClear color+depth.
    void BeginColor(const ColorDesc& desc);

    // SDL_GPU: SDL_EndGPURenderPass. OpenGL: restore FBO + viewport.
    void End();

#ifdef MD_SDL_GPU
    SDL_GPURenderPass* SDLPass() const { return sdl_pass_; }
#endif

private:
    int  saved_vp_[4]  = {};
    bool cull_front_   = false;
#ifdef MD_SDL_GPU
    SDL_GPURenderPass* sdl_pass_ = nullptr;
#endif
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuDrawIndexedIndirect — indexed draw using a GPU-side indirect buffer.
// SDL_GPU: SDL_DrawGPUIndexedPrimitivesIndirect(pass, buf, offset, draw_count)
// ─────────────────────────────────────────────────────────────────────────────
void GpuDrawIndexedIndirect(unsigned int indirect_buf_id, uint32_t draw_count = 1);

// ─────────────────────────────────────────────────────────────────────────────
// GpuStaticBuffer — immutable GPU buffer loaded once from CPU data.
// OpenGL: glBufferData(GL_STATIC_DRAW).
// SDL_GPU: SDL_CreateGPUBuffer(VERTEX|INDEX) + one-shot SDL_UploadToGPUBuffer
//          via temporary transfer buffer (released after upload).
// ─────────────────────────────────────────────────────────────────────────────
class GpuStaticBuffer {
public:
    // gl_target: GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER (used for usage hint).
    // SDL_GPU path maps GL_ELEMENT_ARRAY_BUFFER → INDEX usage, rest → VERTEX.
    void Init(unsigned int gl_target, const void* data, uint32_t size_bytes);
    void Shutdown();

    // OpenGL bind helpers (no-op in SDL_GPU-only builds).
    void Bind(unsigned int gl_target) const;
    void BindVertex(uint32_t slot, uint32_t stride, uint64_t offset = 0) const;

    unsigned int GLBuffer() const { return gl_buf_; }
#ifdef MD_SDL_GPU
    SDL_GPUBuffer* SDLBuffer() const { return sdl_buf_; }
#endif

    // Transfer GL buffer ownership (for MdMesh legacy interop).
    unsigned int Release() { unsigned int id = gl_buf_; gl_buf_ = 0; return id; }

private:
    unsigned int gl_buf_ = 0;
#ifdef MD_SDL_GPU
    SDL_GPUBuffer* sdl_buf_ = nullptr;
#endif
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
    bool   flip_v     = false;

    static GpuSamplerDesc Default()  { return {}; }
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
// GpuTexture — RGBA8 texture + sampler.
// OpenGL: glGenTextures + glTexImage2D + sampler params.
// SDL_GPU: SDL_CreateGPUTexture(R8G8B8A8_UNORM) + SDL_CreateGPUSampler +
//   one-shot SDL_UploadToGPUTexture + optional SDL_GenerateMipmapsForGPUTexture.
// ─────────────────────────────────────────────────────────────────────────────
class GpuTexture {
public:
    bool InitFromFile  (const char* path,              const GpuSamplerDesc& s = {});
    bool InitFromMemory(const uint8_t* rgba8, int w, int h, const GpuSamplerDesc& s = {});
    void Shutdown();

    // OpenGL: glActiveTexture + glBindTexture.
    // SDL_GPU: binding via SDL_BindGPUFragmentSamplers in render pass (Step 6).
    void Bind(uint32_t unit) const;

    int          Width()     const { return w_; }
    int          Height()    const { return h_; }
    unsigned int GLTexture() const { return id_; }
    bool Valid() const {
#ifdef MD_SDL_GPU
        return sdl_tex_ != nullptr;
#else
        return id_ != 0;
#endif
    }
#ifdef MD_SDL_GPU
    SDL_GPUTexture* SDLTexture() const { return sdl_tex_; }
    SDL_GPUSampler* SDLSampler() const { return sdl_sampler_; }
#endif

    // Transfer GL texture ownership (MdMesh legacy interop — OpenGL only).
    unsigned int Release() {
#ifdef MD_OPENGL43_ENABLED
        unsigned int t = id_; id_ = 0; return t;
#else
        return 0;
#endif
    }

private:
    void ApplySampler(const GpuSamplerDesc& s) const; // OpenGL only
    unsigned int id_ = 0;
    int w_ = 0, h_ = 0;
#ifdef MD_SDL_GPU
    SDL_GPUTexture* sdl_tex_     = nullptr;
    SDL_GPUSampler* sdl_sampler_ = nullptr;
#endif
};

#endif // MD_OPENGL43_ENABLED || MD_SDL_GPU
