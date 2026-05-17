#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/platform/md_log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "glad.h"

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#endif

// ── Common helpers ─────────────────────────────────────────────────────────────

static char* ReadTextFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return nullptr; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

#ifdef MD_SDL_GPU

// ── SPIR-V bytecode cache ─────────────────────────────────────────────────────
// Avoids disk reads when the same .spv file is loaded multiple times
// (hot-reload, shared vertex shaders across pipeline variants).

static constexpr int SPV_CACHE_MAX = 64;

struct SpvEntry {
    uint32_t hash   = 0;
    void*    data   = nullptr;
    size_t   size   = 0;
    bool     used   = false;
};

static SpvEntry s_spv_cache[SPV_CACHE_MAX];

static uint32_t SpvHash(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
    return h ? h : 1u;
}

static void* ReadBinaryFile(const char* path, size_t* out_size) {
    uint32_t h = SpvHash(path);
    // Check cache.
    for (int i = 0; i < SPV_CACHE_MAX; ++i) {
        if (s_spv_cache[i].used && s_spv_cache[i].hash == h) {
            *out_size = s_spv_cache[i].size;
            return s_spv_cache[i].data;  // caller must NOT free this pointer
        }
    }
    // Cache miss: read from disk.
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    void* buf = malloc((size_t)len);
    if (!buf) { fclose(f); return nullptr; }
    fread(buf, 1, (size_t)len, f);
    fclose(f);
    *out_size = (size_t)len;
    // Insert into cache (find empty slot).
    for (int i = 0; i < SPV_CACHE_MAX; ++i) {
        if (!s_spv_cache[i].used) {
            s_spv_cache[i] = { h, buf, (size_t)len, true };
            return buf;
        }
    }
    // Cache full — return data without caching (caller must free).
    return buf;
}

// Called by LoadSpvShader ONLY when data did NOT come from cache.
static bool SpvIsCached(const void* ptr) {
    for (int i = 0; i < SPV_CACHE_MAX; ++i)
        if (s_spv_cache[i].used && s_spv_cache[i].data == ptr) return true;
    return false;
}

void MdSpvCache_Shutdown() {
    int freed = 0;
    for (int i = 0; i < SPV_CACHE_MAX; ++i) {
        if (s_spv_cache[i].used) {
            free(s_spv_cache[i].data);
            s_spv_cache[i] = {};
            ++freed;
        }
    }
    if (freed) fprintf(stdout, "[SpvCache] shutdown: freed %d SPIR-V entries\n", freed);
}

int MdSpvCache_Stats(int* out_count) {
    int n = 0;
    for (int i = 0; i < SPV_CACHE_MAX; ++i)
        if (s_spv_cache[i].used) ++n;
    if (out_count) *out_count = n;
    return n;
}


// Derive SPIR-V path: "shaders/pbr.vert" → "shaders/spirv/pbr.vert.spv"
static void MakeSpvPath(char* out, size_t out_sz, const char* glsl_path) {
    const char* slash = strrchr(glsl_path, '/');
    const char* name  = slash ? slash + 1 : glsl_path;
    snprintf(out, out_sz, "shaders/spirv/%s.spv", name);
}

static SDL_GPUVertexElementFormat ToSDLFmt(GpuAttribFmt f) {
    switch (f) {
    case GpuAttribFmt::F1:        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case GpuAttribFmt::F2:        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case GpuAttribFmt::F3:        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case GpuAttribFmt::F4:        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case GpuAttribFmt::U8x4_NORM: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
}

static SDL_GPUPrimitiveType ToSDLPrim(GpuTopology t) {
    switch (t) {
    case GpuTopology::POINTS: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
    case GpuTopology::LINES:  return SDL_GPU_PRIMITIVETYPE_LINELIST;
    default:                  return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    }
}

static SDL_GPUBlendFactor ToSDLBlend(GpuBlendFactor f) {
    switch (f) {
    case GpuBlendFactor::ZERO:                return SDL_GPU_BLENDFACTOR_ZERO;
    case GpuBlendFactor::ONE:                 return SDL_GPU_BLENDFACTOR_ONE;
    case GpuBlendFactor::SRC_COLOR:           return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case GpuBlendFactor::ONE_MINUS_SRC_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case GpuBlendFactor::SRC_ALPHA:           return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case GpuBlendFactor::ONE_MINUS_SRC_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case GpuBlendFactor::DST_ALPHA:           return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case GpuBlendFactor::ONE_MINUS_DST_ALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case GpuBlendFactor::DST_COLOR:           return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case GpuBlendFactor::ONE_MINUS_DST_COLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

static SDL_GPUShader* LoadSpvShader(SDL_GPUDevice* dev,
                                    const char*    spv_path,
                                    SDL_GPUShaderStage stage,
                                    uint32_t num_uniform_bufs,
                                    uint32_t num_storage_bufs,
                                    uint32_t num_samplers) {
    size_t code_size = 0;
    void*  code      = ReadBinaryFile(spv_path, &code_size);
    if (!code) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] SPIR-V not found: %s", spv_path);
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info = {};
    info.code              = (const Uint8*)code;
    info.code_size         = code_size;
    info.entrypoint        = "main";
    info.format            = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage             = stage;
    info.num_uniform_buffers = num_uniform_bufs;
    info.num_storage_buffers = num_storage_bufs;
    info.num_samplers      = num_samplers;
    SDL_GPUShader* sh = SDL_CreateGPUShader(dev, &info);
    // Only free if not owned by the SPIR-V cache.
    if (!SpvIsCached(code)) free(code);
    if (!sh) MD_LOG(MD_LOG_WARNING, "[GpuPipeline] SDL_CreateGPUShader failed: %s", SDL_GetError());
    return sh;
}

#endif // MD_SDL_GPU

// ── GpuPipeline ───────────────────────────────────────────────────────────────

bool GpuPipeline::Create(const Desc& desc) {
    if (!desc.vert_path || !desc.frag_path) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] Create: null shader paths");
        return false;
    }
    raster_ = desc.raster;

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (!dev) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] SDL_GPU not ready");
        return false;
    }

    char spv_vert[256], spv_frag[256];
    MakeSpvPath(spv_vert, sizeof(spv_vert), desc.vert_path);
    MakeSpvPath(spv_frag, sizeof(spv_frag), desc.frag_path);

    SDL_GPUShader* vert_sh = LoadSpvShader(dev, spv_vert,
        SDL_GPU_SHADERSTAGE_VERTEX,
        desc.vert_uniform_bufs, desc.vert_storage_bufs, 0);
    SDL_GPUShader* frag_sh = LoadSpvShader(dev, spv_frag,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        desc.frag_uniform_bufs, desc.frag_storage_bufs, desc.frag_samplers);

    if (!vert_sh || !frag_sh) {
        if (vert_sh) SDL_ReleaseGPUShader(dev, vert_sh);
        if (frag_sh) SDL_ReleaseGPUShader(dev, frag_sh);
        return false;
    }

    // Vertex input state
    SDL_GPUVertexBufferDescription vbds[2] = {};
    vbds[0].slot               = 0;
    vbds[0].pitch              = desc.layout.stride;
    vbds[0].input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbds[0].instance_step_rate = 0;
    const bool has_inst = (desc.layout.inst_stride > 0 && desc.layout.inst_count > 0);
    if (has_inst) {
        vbds[1].slot               = 1;
        vbds[1].pitch              = desc.layout.inst_stride;
        vbds[1].input_rate         = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        vbds[1].instance_step_rate = 0; // SDL_GPU requires 0; INSTANCE input_rate controls stepping
    }

    SDL_GPUVertexAttribute vattribs[16] = {};
    uint32_t total_attribs = 0;
    for (uint32_t i = 0; i < desc.layout.count; ++i, ++total_attribs) {
        vattribs[total_attribs].location    = desc.layout.attribs[i].location;
        vattribs[total_attribs].buffer_slot = 0;
        vattribs[total_attribs].format      = ToSDLFmt(desc.layout.attribs[i].fmt);
        vattribs[total_attribs].offset      = desc.layout.attribs[i].offset;
    }
    if (has_inst) {
        for (uint32_t i = 0; i < desc.layout.inst_count; ++i, ++total_attribs) {
            vattribs[total_attribs].location    = desc.layout.inst_attribs[i].location;
            vattribs[total_attribs].buffer_slot = 1;
            vattribs[total_attribs].format      = ToSDLFmt(desc.layout.inst_attribs[i].fmt);
            vattribs[total_attribs].offset      = desc.layout.inst_attribs[i].offset;
        }
    }

    SDL_GPUVertexInputState vertex_input = {};
    vertex_input.vertex_buffer_descriptions = (desc.layout.stride > 0) ? vbds : nullptr;
    vertex_input.num_vertex_buffers         = (desc.layout.stride > 0) ? (has_inst ? 2u : 1u) : 0u;
    vertex_input.vertex_attributes          = (total_attribs > 0) ? vattribs : nullptr;
    vertex_input.num_vertex_attributes      = total_attribs;

    // Color target (uses swapchain format); skipped for depth_only passes.
    SDL_GPUColorTargetDescription color_target = {};
    if (!desc.depth_only) {
        SDL_GPUTextureFormat fmt = desc.color_format;
        if (fmt == SDL_GPU_TEXTUREFORMAT_INVALID) {
            SDL_Window* win = md::GpuDevice::Get().Window();
            fmt = SDL_GetGPUSwapchainTextureFormat(dev, win);
        }
        color_target.format = fmt;
        if (desc.raster.blend_enable) {
            color_target.blend_state.enable_blend          = true;
            color_target.blend_state.src_color_blendfactor = ToSDLBlend(desc.raster.src_factor);
            color_target.blend_state.dst_color_blendfactor = ToSDLBlend(desc.raster.dst_factor);
            color_target.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
            color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            color_target.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
            color_target.blend_state.color_write_mask      = 0xF;
        }
    }

    SDL_GPUGraphicsPipelineTargetInfo target_info = {};
    target_info.color_target_descriptions = desc.depth_only ? nullptr : &color_target;
    target_info.num_color_targets         = desc.depth_only ? 0u : 1u;
    if (desc.has_depth_target || desc.depth_only) {
        target_info.depth_stencil_format       = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        target_info.has_depth_stencil_target   = true;
    }

    SDL_GPUGraphicsPipelineCreateInfo ci = {};
    ci.vertex_shader          = vert_sh;
    ci.fragment_shader        = frag_sh;
    ci.vertex_input_state     = vertex_input;
    ci.primitive_type         = ToSDLPrim(desc.raster.topology);
    ci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode  = desc.raster.cull_back
                                     ? SDL_GPU_CULLMODE_BACK
                                     : SDL_GPU_CULLMODE_NONE;
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    ci.depth_stencil_state.compare_op         = desc.raster.depth_compare_op;
    ci.depth_stencil_state.enable_depth_test  = desc.raster.depth_test  ? true : false;
    ci.depth_stencil_state.enable_depth_write = desc.raster.depth_write ? true : false;
    ci.target_info = target_info;

    sdl_pipeline_ = SDL_CreateGPUGraphicsPipeline(dev, &ci);

    // Shaders are consumed by the pipeline; release immediately.
    SDL_ReleaseGPUShader(dev, vert_sh);
    SDL_ReleaseGPUShader(dev, frag_sh);

    if (!sdl_pipeline_) {
        MD_LOG(MD_LOG_WARNING, "[GpuPipeline] SDL_CreateGPUGraphicsPipeline failed: %s",
               SDL_GetError());
        return false;
    }
    MD_LOG(MD_LOG_INFO, "[GpuPipeline] SDL_GPU pipeline created: %s / %s",
           desc.vert_path, desc.frag_path);
    return true;
#else
    (void)desc;
    return false;
#endif
}

void GpuPipeline::Destroy() {
#ifdef MD_SDL_GPU
    if (sdl_pipeline_) {
        SDL_ReleaseGPUGraphicsPipeline(md::GpuDevice::Get().SDLDevice(), sdl_pipeline_);
        sdl_pipeline_ = nullptr;
    }
#endif
}

int GpuPipeline::UniformLoc(const char* name) const {
    (void)name; return -1;
}

// ── GpuComputePipeline ────────────────────────────────────────────────────────

bool GpuComputePipeline::Create(const Desc& desc) {
    if (!desc.glsl_path) {
        MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] Create: null glsl_path");
        return false;
    }

#ifdef MD_SDL_GPU
    SDL_GPUDevice* dev = md::GpuDevice::Get().SDLDevice();
    if (dev) {
        char spv[256];
        MakeSpvPath(spv, sizeof(spv), desc.glsl_path);
        size_t code_size = 0;
        void*  code = ReadBinaryFile(spv, &code_size);
        if (code) {
            SDL_GPUComputePipelineCreateInfo ci = {};
            ci.code                           = (const Uint8*)code;
            ci.code_size                      = code_size;
            ci.entrypoint                     = "main";
            ci.format                         = SDL_GPU_SHADERFORMAT_SPIRV;
            ci.num_uniform_buffers            = desc.num_uniform_buffers;
            ci.num_readonly_storage_buffers   = desc.num_readonly_storage_buffers;
            ci.num_readwrite_storage_buffers  = desc.num_readwrite_storage_buffers;
            ci.num_readonly_storage_textures  = desc.num_readonly_storage_textures;
            ci.num_readwrite_storage_textures = desc.num_readwrite_storage_textures;
            ci.num_samplers                   = desc.num_samplers;
            ci.threadcount_x                  = desc.threadcount_x;
            ci.threadcount_y                  = desc.threadcount_y;
            ci.threadcount_z                  = desc.threadcount_z;
            sdl_pipeline_ = SDL_CreateGPUComputePipeline(dev, &ci);
            // Respect SPIR-V cache: only free if not cache-owned.
            if (!SpvIsCached(code)) free(code);
            if (!sdl_pipeline_)
                MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] SDL_CreateGPUComputePipeline %s: %s",
                       desc.glsl_path, SDL_GetError());
            else
                MD_LOG(MD_LOG_INFO, "[GpuComputePipeline] SDL_GPU pipeline: %s", desc.glsl_path);
        } else {
            MD_LOG(MD_LOG_WARNING, "[GpuComputePipeline] SPIR-V not found: %s", spv);
        }
    }
    return sdl_pipeline_ != nullptr;
#else
    return false;
#endif
}

void GpuComputePipeline::Destroy() {
#ifdef MD_SDL_GPU
    if (sdl_pipeline_) {
        SDL_ReleaseGPUComputePipeline(md::GpuDevice::Get().SDLDevice(), sdl_pipeline_);
        sdl_pipeline_ = nullptr;
    }
#endif
}

int GpuComputePipeline::UniformLoc(const char* name) const {
    (void)name;
    return -1; // SDL_GPU: use PushUniforms instead
}
