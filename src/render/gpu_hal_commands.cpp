#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/platform/md_log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "glad.h"

#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#endif

// ── GpuCommandBuffer ─────────────────────────────────────────────────────────

void GpuCommandBuffer::BindPipeline(GpuPipeline* p) {
    pipeline_ = p;
    if (!p) return;

#ifdef MD_SDL_GPU
    if (sdl_cmd_) {
        if (sdl_pass_) SDL_BindGPUGraphicsPipeline(sdl_pass_, p->sdl_pipeline_);
        return;  // dual-backend: skip GL path when SDL_GPU command buffer is active
    }
#endif
}

void GpuCommandBuffer::BindVertexBuffer(GpuVertexBuffer* buf) {
#ifdef MD_SDL_GPU
    if (sdl_cmd_) {
        if (sdl_pass_ && buf && buf->sdl_buf_) {
            SDL_GPUBufferBinding binding = {};
            binding.buffer = buf->sdl_buf_;
            binding.offset = 0;
            SDL_BindGPUVertexBuffers(sdl_pass_, 0, &binding, 1);
        }
        return;
    }
#endif
    (void)buf;
}

void GpuCommandBuffer::SetUniformMat4(int loc, const float* m16) {
    (void)loc; (void)m16;
}

void GpuCommandBuffer::SetUniformVec3(int loc, const float* v3) {
    (void)loc; (void)v3;
}

void GpuCommandBuffer::Draw(uint32_t vertex_count, uint32_t first_vertex) {
#ifdef MD_SDL_GPU
    if (sdl_cmd_) {
        if (sdl_pass_) SDL_DrawGPUPrimitives(sdl_pass_, vertex_count, 1, first_vertex, 0);
        return;
    }
#endif
    (void)vertex_count; (void)first_vertex;
}

void GpuCommandBuffer::EndPass() {
#ifdef MD_SDL_GPU
    if (sdl_cmd_) {
        if (sdl_pass_) { SDL_EndGPURenderPass(sdl_pass_); sdl_pass_ = nullptr; }
        sdl_cmd_ = nullptr;
        pipeline_ = nullptr;
        return;  // dual-backend: skip GL path when SDL_GPU command buffer is active
    }
#endif
    pipeline_ = nullptr;
}

// ── GpuCommandBuffer + GpuRenderPass — SDL_GPU paths ─────────────────────────

#ifdef MD_SDL_GPU

void GpuCommandBuffer::BeginColorPass(const ColorPassDesc& desc) {
    sdl_cmd_ = desc.cmd;

    SDL_GPUColorTargetInfo color_info = {};
    color_info.texture     = desc.color_tex;
    color_info.clear_color = { desc.clear_color[0], desc.clear_color[1],
                               desc.clear_color[2], desc.clear_color[3] };
    color_info.load_op  = desc.load_color ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    color_info.cycle    = false;

    if (desc.depth_tex) {
        SDL_GPUDepthStencilTargetInfo depth_info = {};
        depth_info.texture          = desc.depth_tex;
        depth_info.clear_depth      = desc.clear_depth;
        depth_info.load_op          = desc.load_depth ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        depth_info.store_op         = SDL_GPU_STOREOP_STORE;
        depth_info.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth_info.cycle            = false;
        sdl_pass_ = SDL_BeginGPURenderPass(sdl_cmd_, &color_info, 1, &depth_info);
    } else {
        sdl_pass_ = SDL_BeginGPURenderPass(sdl_cmd_, &color_info, 1, nullptr);
    }
}

void GpuCommandBuffer::BindFragmentSamplers(uint32_t first_slot,
                                             const SDL_GPUTextureSamplerBinding* bindings,
                                             uint32_t count) {
    if (sdl_pass_)
        SDL_BindGPUFragmentSamplers(sdl_pass_, first_slot, bindings, count);
}

void GpuCommandBuffer::PushVertexUniforms(uint32_t slot, const void* data, uint32_t size_bytes) {
    if (sdl_cmd_)
        SDL_PushGPUVertexUniformData(sdl_cmd_, slot, data, size_bytes);
}

void GpuCommandBuffer::PushFragmentUniforms(uint32_t slot, const void* data, uint32_t size_bytes) {
    if (sdl_cmd_)
        SDL_PushGPUFragmentUniformData(sdl_cmd_, slot, data, size_bytes);
}

void GpuRenderPass::BeginDepthOnly(SDL_GPUCommandBuffer* cmd, const DepthDesc& desc) {
    SDL_GPUDepthStencilTargetInfo depth_info = {};
    depth_info.texture          = desc.target->SDLTexture();
    depth_info.clear_depth      = desc.clear_depth;
    depth_info.load_op          = desc.load_depth ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    // DONT_CARE pattern: if caller signals depth is not read after this pass
    // (e.g. shadow depth already resolved into EVSM moment map), skip the
    // memory write → saves ~1MB/cascade × 3 cascades of DRAM bandwidth.
    depth_info.store_op         = desc.discard_after
                                ? SDL_GPU_STOREOP_DONT_CARE
                                : SDL_GPU_STOREOP_STORE;
    depth_info.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_info.cycle            = false;
    sdl_pass_  = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth_info);
    if (!sdl_pass_) fprintf(stderr, "[GPU] BeginDepthOnly FAILED: %s\n", SDL_GetError());
    cull_front_ = desc.cull_front; // stored for symmetry; SDL_GPU cull is pipeline-configured
}

#endif // MD_SDL_GPU

// ── GpuRenderPass::BeginColor (dual-backend) ─────────────────────────────────

void GpuRenderPass::BeginColor(const ColorDesc& desc) {
#ifdef MD_SDL_GPU
    if (desc.cmd) {
        uint32_t sw = 0, sh = 0;
        SDL_GPUTexture* color_tex =
            md::GpuDevice::Get().AcquireSwapchainTexture(desc.cmd, &sw, &sh);
        if (color_tex) {
            SDL_GPUColorTargetInfo color_info = {};
            color_info.texture     = color_tex;
            color_info.clear_color = { desc.clear[0], desc.clear[1],
                                       desc.clear[2], desc.clear[3] };
            color_info.load_op  = SDL_GPU_LOADOP_CLEAR;
            color_info.store_op = SDL_GPU_STOREOP_STORE;
            color_info.cycle    = false;

            if (desc.depth) {
                SDL_GPUDepthStencilTargetInfo depth_info = {};
                depth_info.texture          = desc.depth->SDLTexture();
                depth_info.clear_depth      = desc.clear_depth;
                depth_info.load_op          = desc.load_depth ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
                depth_info.store_op         = SDL_GPU_STOREOP_STORE;
                depth_info.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
                depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                depth_info.cycle            = false;
                sdl_pass_ = SDL_BeginGPURenderPass(desc.cmd, &color_info, 1, &depth_info);
            } else {
                sdl_pass_ = SDL_BeginGPURenderPass(desc.cmd, &color_info, 1, nullptr);
            }
            if (!sdl_pass_) fprintf(stderr, "[GPU] BeginColor FAILED: %s\n", SDL_GetError());
        }
    }
#endif
    (void)desc;
}

// ── GpuRenderPass::End ────────────────────────────────────────────────────────

void GpuRenderPass::End() {
#ifdef MD_SDL_GPU
    if (sdl_pass_) {
        SDL_EndGPURenderPass(sdl_pass_);
        sdl_pass_ = nullptr;
    }
#endif
    cull_front_ = false;
}

// ── GpuComputePass ────────────────────────────────────────────────────────────

void GpuComputePass::Begin(GpuComputePipeline* pipeline, const StorageBindings& bindings) {
    pipeline_ = pipeline;
#ifdef MD_SDL_GPU
    sdl_cmd_ = bindings.cmd;
    if (sdl_cmd_ && pipeline && pipeline->sdl_pipeline_) {
        sdl_pass_ = SDL_BeginGPUComputePass(
            sdl_cmd_,
            nullptr, 0,
            bindings.rw_buffers, bindings.num_rw_buffers
        );
        if (sdl_pass_) {
            SDL_BindGPUComputePipeline(sdl_pass_, pipeline->sdl_pipeline_);
            if (bindings.num_ro_buffers > 0)
                SDL_BindGPUComputeStorageBuffers(
                    sdl_pass_, 0, bindings.ro_buffers, bindings.num_ro_buffers);
        }
    }
#else
    (void)bindings;
#endif
}

void GpuComputePass::SetUniformFloat(int loc, float v) {
    (void)loc; (void)v;
}

void GpuComputePass::SetUniformInt(int loc, int v) {
    (void)loc; (void)v;
}

void GpuComputePass::SetUniformVec3(int loc, const float* v3) {
    (void)loc; (void)v3;
}

void GpuComputePass::SetUniformVec4Array(int loc, const float* v4, int count) {
    (void)loc; (void)v4; (void)count;
}

#ifdef MD_SDL_GPU
void GpuComputePass::PushUniforms(uint32_t slot, const void* data, uint32_t size_bytes) {
    if (sdl_cmd_)
        SDL_PushGPUComputeUniformData(sdl_cmd_, slot, data, size_bytes);
}
#endif

void GpuComputePass::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
#ifdef MD_SDL_GPU
    if (sdl_pass_) SDL_DispatchGPUCompute(sdl_pass_, gx, gy, gz);
#endif
}

void GpuComputePass::End(uint32_t barrier_flags) {
#ifdef MD_SDL_GPU
    if (sdl_pass_) {
        SDL_EndGPUComputePass(sdl_pass_);
        sdl_pass_ = nullptr;
    }
    sdl_cmd_ = nullptr;
#endif
    (void)barrier_flags;
    pipeline_ = nullptr;
}
