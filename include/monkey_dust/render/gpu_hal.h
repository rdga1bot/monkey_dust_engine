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
    U8x4_NORM,            // uint8×4 normalized [0,1] → vec4
    U8x4                  // uint8×4 raw (bone joint indices → uvec4)
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
// slot 0: per-vertex attributes (stride > 0 required).
// slot 1: second buffer — per-instance (inst_per_vertex=false, default) OR
//         per-vertex (inst_per_vertex=true) when inst_stride > 0 && inst_count > 0.
struct GpuVertexLayout {
    GpuVertexAttrib attribs[8] = {};
    uint32_t        count      = 0;
    uint32_t        stride     = 0;   // bytes per vertex (slot 0)
    // Optional second binding (slot 1).
    GpuVertexAttrib inst_attribs[8] = {};
    uint32_t        inst_count      = 0;
    uint32_t        inst_stride     = 0;    // bytes per element; 0 = disabled
    bool            inst_per_vertex = false; // true = VERTEX rate; false = INSTANCE rate
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
#ifdef MD_SDL_GPU
    // Depth compare function. Default LESS_OR_EQUAL matches the prepass write.
    // Set to EQUAL for main pass after an Early-Z prepass (depth_write=false).
    SDL_GPUCompareOp depth_compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
#endif
};

// ── ShaderFeature — SPIR-V variant selection ──────────────────────────────────
// Vulkan adaptation: replaces VkSpecializationInfo with pre-compiled .spv variants.
// Each bit selects compile-time constants via glslc -D, producing optimized SPIR-V
// (dead code elimination, loop unrolling) equivalent to VkSpecializationInfo.
//
// Naming convention: basename + suffix per active flag, e.g.:
//   SF_None                      → "mesh.spv"
//   SF_Skinned                   → "mesh_skinned.spv"
//   SF_Skinned | SF_Shadows      → "mesh_skinned_shadow.spv"
//   SF_Shadows | SF_PCF_HIGH     → "mesh_shadow_pcfhigh.spv"
// Variants must be pre-compiled by scripts/compile_shaders.sh.
enum ShaderFeature : uint32_t {
    SF_None      = 0,
    SF_Skinned   = 1u << 0,  // GPU skeletal animation via SSBO bones (binding 4)
    SF_Shadows   = 1u << 1,  // CSM shadow sampling (3 cascades)
    SF_AlphaTest = 1u << 2,  // frag discard for alpha-tested geometry
    // VBfA-R9: character material variants (same animated.vert, different FS)
    SF_Emissive  = 1u << 3,  // animated_emissive.frag — glowing NPC (fire/magic)
    SF_Dissolve  = 1u << 4,  // animated_dissolve.frag — death/despawn noise clip
    SF_Ice       = 1u << 5,  // animated_ice.frag (future: frozen NPC effect)
    // Specialization constant variants (glslc -DFLAG=1):
    SF_PCF_HIGH  = 1u << 6,  // -DPCF_SAMPLES=16  (vs default 4) — higher shadow quality
    SF_SSAO_INT  = 1u << 7,  // -DSSAO_INTEGRATION — forward pass reads SSAO buffer
    SF_IBL       = 1u << 8,  // -DIBL_ENABLED — Image-Based Lighting for PBR materials
    SF_DITHERED  = 1u << 9,  // -DDITHERED_TRANSPARENCY — ordered dithering alpha
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
        uint32_t        shader_features = SF_None; // bitmask of ShaderFeature — selects .spv variant
        GpuVertexLayout layout;
        GpuRasterState  raster;
#ifdef MD_SDL_GPU
        // Shader resource counts required by SDL_GPUShaderCreateInfo.
        // Must match the SPIR-V descriptor declarations exactly.
        uint32_t vert_uniform_bufs = 0;
        uint32_t vert_samplers     = 0;
        uint32_t vert_storage_bufs = 0;
        uint32_t frag_uniform_bufs = 0;
        uint32_t frag_storage_bufs = 0;
        uint32_t frag_samplers     = 0;
        bool     has_depth_target  = true;  // set false for 2D/HUD passes
        bool     depth_only        = false; // depth-only pass (shadow): no color target
        // Override swapchain color format (e.g. SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
        // for intermediate render targets). INVALID (0) = use swapchain format.
        SDL_GPUTextureFormat color_format = SDL_GPU_TEXTUREFORMAT_INVALID;
#endif
    };

    bool Create(const Desc& desc);
    void Destroy();
    // Re-read SPV from disk and recreate pipeline. Safe to call mid-frame on next frame boundary.
    bool Reload();

    // OpenGL: glGetUniformLocation. SDL_GPU: always returns -1 (use PushUniforms instead).
    int UniformLoc(const char* name) const;
#ifdef MD_SDL_GPU
    SDL_GPUGraphicsPipeline* SDLPipeline() const { return sdl_pipeline_; }
#endif

private:
    friend class GpuCommandBuffer;
#ifdef MD_SDL_GPU
    SDL_GPUGraphicsPipeline* sdl_pipeline_ = nullptr;
#endif
    GpuRasterState raster_ = {};
    Desc           desc_   = {};  // stored at Create() for Reload()
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
    struct Desc {
        const char* glsl_path = nullptr; // OpenGL GLSL source; SPIR-V path derived by MakeSpvPath
        // SDL_GPU resource counts — must match SPIR-V declarations exactly.
        uint32_t num_uniform_buffers            = 0;
        uint32_t num_readonly_storage_buffers   = 0;
        uint32_t num_readwrite_storage_buffers  = 0;
        uint32_t num_readonly_storage_textures  = 0;
        uint32_t num_readwrite_storage_textures = 0;
        uint32_t num_samplers                   = 0;
        // Must match layout(local_size_x/y/z) in the compute shader.
        uint32_t threadcount_x = 64;
        uint32_t threadcount_y = 1;
        uint32_t threadcount_z = 1;
    };

    bool Create(const Desc& desc);
    void Destroy();
    // OpenGL: glGetUniformLocation. SDL_GPU: always -1 (use PushUniforms instead).
    int  UniformLoc(const char* name) const;

#ifdef MD_SDL_GPU
    SDL_GPUComputePipeline* SDLComputePipeline() const { return sdl_pipeline_; }
#endif

private:
    friend class GpuComputePass;
    unsigned int program_ = 0;
#ifdef MD_SDL_GPU
    SDL_GPUComputePipeline* sdl_pipeline_ = nullptr;
#endif
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuComputePass::StorageBindings — declared outside the class so it can be
// used as a default argument (GCC rejects nested-struct DMIs in that context).
// ─────────────────────────────────────────────────────────────────────────────
struct GpuComputeStorageBindings {
#ifdef MD_SDL_GPU
    SDL_GPUCommandBuffer* cmd = nullptr;
    // Read-write storage buffers — declared to SDL_BeginGPUComputePass.
    // Maps slot index to SPIR-V set=1 read-write bindings in ascending binding order.
    SDL_GPUStorageBufferReadWriteBinding rw_buffers[8] = {};
    uint32_t                             num_rw_buffers = 0;
    // Read-only storage buffers — bound via SDL_BindGPUComputeStorageBuffers.
    SDL_GPUBuffer* ro_buffers[8] = {};
    uint32_t       num_ro_buffers = 0;
#endif
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuComputePass — scoped compute dispatch with explicit memory barrier.
//
// OpenGL usage:
//   pass.Begin(&pipeline);        // bind compute program
//   pass.SetUniform*(...);        // set uniforms via GL locations
//   pass.Dispatch(gx, gy, gz);
//   pass.End(BARRIER_STORAGE);    // glMemoryBarrier + unbind
//
// SDL_GPU usage:
//   GpuComputeStorageBindings b; b.cmd = cmd; b.rw_buffers[0] = {sdl_buf, false}; b.num_rw_buffers = 1;
//   pass.Begin(&pipeline, b);     // SDL_BeginGPUComputePass + SDL_BindGPUComputePipeline
//   pass.PushUniforms(0, &data, sizeof(data));
//   pass.Dispatch(gx, gy, gz);    // SDL_DispatchGPUCompute
//   pass.End();                   // SDL_EndGPUComputePass (barriers implicit)
// ─────────────────────────────────────────────────────────────────────────────
class GpuComputePass {
public:
    static constexpr uint32_t BARRIER_STORAGE         = 1u; // GL_SHADER_STORAGE_BARRIER_BIT
    static constexpr uint32_t BARRIER_COMMAND         = 2u; // GL_COMMAND_BARRIER_BIT
    static constexpr uint32_t BARRIER_STORAGE_COMMAND = 3u; // both — for indirect draw output

    using StorageBindings = GpuComputeStorageBindings; // backward-compat alias

    // Bind pipeline and (SDL_GPU) open compute pass.
    // bindings is required for SDL_GPU; ignored in OpenGL.
    void Begin(GpuComputePipeline* pipeline, const StorageBindings& bindings = StorageBindings{});

    // OpenGL named-uniform setters. SDL_GPU: no-ops (use PushUniforms instead).
    void SetUniformFloat    (int loc, float v);
    void SetUniformInt      (int loc, int v);
    void SetUniformVec3     (int loc, const float* v3);
    void SetUniformVec4Array(int loc, const float* v4, int count);

#ifdef MD_SDL_GPU
    // Push uniform data (UBO slot) for the compute shader.
    void PushUniforms(uint32_t slot, const void* data, uint32_t size_bytes);
#endif

    void Dispatch(uint32_t gx, uint32_t gy, uint32_t gz);

    // barrier_flags: BARRIER_* constants (OpenGL). SDL_GPU barriers are implicit.
    void End(uint32_t barrier_flags = BARRIER_STORAGE);

private:
    GpuComputePipeline* pipeline_ = nullptr;
#ifdef MD_SDL_GPU
    SDL_GPUCommandBuffer* sdl_cmd_  = nullptr;
    SDL_GPUComputePass*   sdl_pass_ = nullptr;
#endif
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
        float clear_depth  = 1.0f;
        bool  cull_front   = false; // GL_FRONT for shadow bias (Peter-Panning fix)
        // Vulkan LOAD/STORE DONT_CARE pattern: set discard_after=true when depth is
        // not needed after this pass (e.g. shadow depth → resolved into EVSM moments).
        bool  discard_after = false;
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
        bool             load_depth  = false;   // true = LOAD (e.g. after Early-Z prepass)
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
// Sentinel gl_target value for GpuStaticBuffer::Init/InitEmpty — requests
// SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ instead of VERTEX/INDEX, so the
// resulting buffer can be bound via SDL_BindGPUFragmentStorageBuffers (a
// StructuredBuffer<T> in shader code). Doesn't collide with any real GL
// buffer-target enum (those are all small, e.g. GL_ARRAY_BUFFER=0x8892).
// Lets read-only per-chunk data (e.g. terrain_chunk.h's steepness_ssbo) ride
// the SAME GpuUploadBatch used for vbo/ibo/skirt — one shared submit instead
// of a second per-chunk synchronous upload (see GpuUploadBatch doc comment
// for why that matters on Intel ANV).
static constexpr unsigned int GPU_TARGET_STORAGE = 0xFFFFFFFEu;

class GpuStaticBuffer {
public:
    // gl_target: GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER (used for usage hint),
    // or GPU_TARGET_STORAGE for a fragment-storage-buffer-bindable resource.
    // SDL_GPU path maps GL_ELEMENT_ARRAY_BUFFER → INDEX usage,
    // GPU_TARGET_STORAGE → GRAPHICS_STORAGE_READ, rest → VERTEX.
    void Init(unsigned int gl_target, const void* data, uint32_t size_bytes);
    // Creates the GPU-side buffer only, no data upload — pair with GpuUploadBatch::Add()
    // (gpu_hal.h below) to fill it. Use when uploading many small buffers at once
    // (e.g. per-chunk terrain VBO/IBO/skirt/LOD) — Init()'s own per-call transfer
    // buffer + command buffer submit does not scale to thousands of calls (see
    // GpuUploadBatch doc comment).
    void InitEmpty(unsigned int gl_target, uint32_t size_bytes);
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
// GpuUploadBatch — merges N GpuStaticBuffer uploads into ONE transfer buffer +
// ONE command buffer submit, instead of GpuStaticBuffer::Init's one-of-each
// per call. Needed whenever a loop uploads many small buffers back to back
// (e.g. a chunk's vbo+ibo+3×ibo_lod+skirt_vbo+skirt_ibo = 7 buffers, repeated
// per chunk): 4096 chunks × 7 unbatched Init() calls (each its own
// CreateGPUTransferBuffer + AcquireCommandBuffer + Submit) was enough one-shot
// command-buffer churn to corrupt Intel ANV driver-internal state and abort
// mid-vkAllocateCommandBuffers (observed: editor's full 64×64 world 3D View
// tab, "malloc(): corrupted top size" right as the last chunks finish).
// Usage: Begin(total_bytes) → Add() once per buffer (Add() creates the GPU-side
// buffer via InitEmpty and copies into the batch's mapped region) → End()
// (one copy pass, one submit). MAX_ITEMS fixed array — no heap allocation.
class GpuUploadBatch {
public:
    static constexpr int MAX_ITEMS = 16;

    bool Begin(uint32_t total_bytes);
    void Add(GpuStaticBuffer& buf, unsigned int gl_target, const void* data, uint32_t size_bytes);
    void End();

private:
#ifdef MD_SDL_GPU
    SDL_GPUTransferBuffer* transfer_ = nullptr;
    uint8_t*               map_      = nullptr;
    struct Item { SDL_GPUBuffer* dst; uint32_t offset; uint32_t size; };
    Item items_[MAX_ITEMS];
#endif
    uint32_t cursor_      = 0;
    uint32_t total_bytes_ = 0;
    int      item_count_  = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuSamplerDesc — texture sampling parameters.
// SDL_GPU: SDL_GPUSamplerCreateInfo
// ─────────────────────────────────────────────────────────────────────────────
struct GpuSamplerDesc {
    enum class Filter : uint8_t { NEAREST, LINEAR, LINEAR_MIPMAP };
    enum class Wrap   : uint8_t { REPEAT, CLAMP_TO_EDGE };

    Filter min_filter  = Filter::LINEAR_MIPMAP;
    Filter mag_filter  = Filter::LINEAR;
    Wrap   wrap_s      = Wrap::REPEAT;
    Wrap   wrap_t      = Wrap::REPEAT;
    bool   gen_mipmap  = false;
    bool   flip_v      = false;
    // L2-style GL2TextureDetail: +0=full, +1=half-res mip, +2=quarter-res mip.
    // Passed to SDL_GPUSamplerCreateInfo::mip_lod_bias.
    float  mip_lod_bias = 0.f;

    static GpuSamplerDesc Default()  { return {}; }
    static GpuSamplerDesc PixelArt() {
        GpuSamplerDesc s;
        s.min_filter = Filter::LINEAR_MIPMAP; s.mag_filter = Filter::NEAREST;
        s.wrap_s = Wrap::CLAMP_TO_EDGE; s.wrap_t = Wrap::CLAMP_TO_EDGE;
        s.gen_mipmap = true; s.flip_v = true;
        return s;
    }
    static GpuSamplerDesc Lut() {
        GpuSamplerDesc s;
        s.min_filter = Filter::LINEAR; s.mag_filter = Filter::LINEAR;
        s.wrap_s = Wrap::CLAMP_TO_EDGE; s.wrap_t = Wrap::CLAMP_TO_EDGE;
        return s;
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
    // Allocates a COLOR_TARGET|SAMPLER texture with NO data upload (SDL_GPU
    // only) — for render-to-texture targets whose first write is a render
    // pass, not CPU data (e.g. TerrainRenderer::BakeAlbedo). InitFromMemory's
    // zero-fill-then-upload does its own synchronous acquire/copy/submit
    // cycle per call; calling it once per chunk (81+) measurably regressed
    // startup time (confirmed: ~4s baseline -> ~16s) for content that gets
    // overwritten by the very next render pass anyway. mip levels (if
    // s.gen_mipmap) are left uninitialized until the caller's own render
    // pass + SDL_GenerateMipmapsForGPUTexture populate them.
    bool InitRenderTarget(int w, int h, const GpuSamplerDesc& s = {});
    // Load a 2D texture array from multiple BC3/DXT5 DDS files (SDL_GPU only).
    // All files must have identical dimensions, format, and mip count.
    bool InitFromDDSArray(const char* const* paths, int count, const GpuSamplerDesc& s = {});
    void Shutdown();

    // OpenGL: glActiveTexture + glBindTexture.
    // SDL_GPU: binding via SDL_BindGPUFragmentSamplers in render pass (Step 6).
    void Bind(uint32_t unit) const;

    int          Width()     const { return w_; }
    int          Height()    const { return h_; }
    unsigned int GLTexture() const { return id_; }
    bool Valid() const {
#ifdef MD_SDL_GPU
        return sdl_tex_ != nullptr || id_ != 0;
#else
        return id_ != 0;
#endif
    }
#ifdef MD_SDL_GPU
    SDL_GPUTexture* SDLTexture() const { return sdl_tex_; }
    SDL_GPUSampler* SDLSampler() const { return sdl_sampler_; }
    // Transfer SDL_GPU ownership out of this object (caller is responsible for release).
    SDL_GPUTexture* TakeSDLTexture() { SDL_GPUTexture* t = sdl_tex_; sdl_tex_ = nullptr; return t; }
    SDL_GPUSampler* TakeSDLSampler() { SDL_GPUSampler* s = sdl_sampler_; sdl_sampler_ = nullptr; return s; }
#endif

    // Transfer GL texture ownership.
    unsigned int Release() { unsigned int t = id_; id_ = 0; return t; }

private:
    void ApplySampler(const GpuSamplerDesc& s) const; // OpenGL only
    unsigned int id_ = 0;
    int w_ = 0, h_ = 0;
#ifdef MD_SDL_GPU
    SDL_GPUTexture* sdl_tex_     = nullptr;
    SDL_GPUSampler* sdl_sampler_ = nullptr;
#endif
};

// ── SPIR-V bytecode cache ─────────────────────────────────────────────────────
// GpuPipeline::Create() caches SPIR-V bytecode by path (FNV-1a hash) so that
// repeated pipeline creation (hot-reload, multiple passes sharing a shader)
// reads from memory instead of disk.  MAX 64 entries per session.
//
// MdSpvCache_Shutdown(): frees all cached bytecode buffers. Call at app exit.
// MdSpvCache_Stats(out): returns total cached count; fills *out if non-null.
void MdSpvCache_Shutdown();
// Evict one SPIR-V entry so the next Create/Reload re-reads it from disk.
void MdSpvCache_Invalidate(const char* glsl_path);
int  MdSpvCache_Stats(int* out_count = nullptr);

// Pipeline object cache (VkPipelineCache SDL_GPU adaptation).
// Caches SDL_GPUGraphicsPipeline objects by (vert+frag+features+raster) hash.
void MdPipeCache_Shutdown();
void MdPipeCache_Invalidate(const char* vert_path, const char* frag_path);

