#include <monkey_dust/render/npc_gpu_culler.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/render/gpu_device.h>
#include <cstdio>
#include <cstring>

namespace md {

// ── Singleton ─────────────────────────────────────────────────────────────────
NpcGpuCuller& NpcGpuCuller::Get() {
    static NpcGpuCuller inst;
    return inst;
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────

bool NpcGpuCuller::Init() {
    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    if (!dev) return false;

    // Compute pipeline.
    GpuComputePipeline::Desc cd;
    cd.glsl_path                    = "shaders/npc_cull.comp";
    cd.num_readonly_storage_buffers  = 1;   // set=0 binding=0: NPC positions
    cd.num_readwrite_storage_buffers = 1;   // set=1 binding=0: visibility output
    cd.num_uniform_buffers           = 1;   // set=2 binding=0: CullUBO
    cd.threadcount_x                 = 64;
    cd.threadcount_y                 = 1;
    cd.threadcount_z                 = 1;
    if (!pipeline_.Create(cd)) {
        fprintf(stderr, "[NpcGpuCuller] pipeline create failed\n");
        return false;
    }

    const uint32_t pos_sz  = (uint32_t)(MAX_NPCS * 2 * sizeof(float));  // vec2[64]
    const uint32_t vis_sz  = (uint32_t)(MAX_NPCS     * sizeof(uint32_t)); // uint[64]

    // RO position buffer (device-side, filled each frame via upload transfer).
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    bci.size  = pos_sz;
    pos_buf_ = SDL_CreateGPUBuffer(dev, &bci);

    // RW visibility buffer: written by compute, then downloaded via copy pass.
    // READ+WRITE needed so the download copy pass can read it as a source.
    bci.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    bci.size  = vis_sz;
    vis_buf_ = SDL_CreateGPUBuffer(dev, &bci);

    // Upload transfer buffer (CPU→GPU positions).
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size  = pos_sz;
    upload_buf_ = SDL_CreateGPUTransferBuffer(dev, &tci);

    // Download transfer buffer (GPU→CPU visibility).
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size  = vis_sz;
    dl_buf_ = SDL_CreateGPUTransferBuffer(dev, &tci);

    if (!pos_buf_ || !vis_buf_ || !upload_buf_ || !dl_buf_) {
        fprintf(stderr, "[NpcGpuCuller] buffer allocation failed\n");
        return false;
    }

    ready_ = true;
    fprintf(stdout, "[NpcGpuCuller] init: compute cull for %d NPCs\n", MAX_NPCS);
    return true;
}

void NpcGpuCuller::Shutdown() {
    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    if (dev) {
        if (pos_buf_)    { SDL_ReleaseGPUBuffer(dev, pos_buf_);                pos_buf_    = nullptr; }
        if (vis_buf_)    { SDL_ReleaseGPUBuffer(dev, vis_buf_);                vis_buf_    = nullptr; }
        if (upload_buf_) { SDL_ReleaseGPUTransferBuffer(dev, upload_buf_);     upload_buf_ = nullptr; }
        if (dl_buf_)     { SDL_ReleaseGPUTransferBuffer(dev, dl_buf_);         dl_buf_     = nullptr; }
    }
    pipeline_.Destroy();
    ready_ = false;
}

// ── Cull ──────────────────────────────────────────────────────────────────────

int NpcGpuCuller::Cull(const float* npc_x, const float* npc_z, int count,
                        const float* mvp16, float npc_radius) {
    last_count_   = 0;
    last_visible_ = 0;
    if (!ready_ || !npc_x || !npc_z || count <= 0) return 0;

    SDL_GPUDevice* dev = GpuDevice::Get().SDLDevice();
    // Acquire a dedicated command buffer for the compute dispatch.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) return 0;

    const int n = count < MAX_NPCS ? count : MAX_NPCS;

    // ── Upload NPC positions ──────────────────────────────────────────────────
    {
        float* ptr = (float*)SDL_MapGPUTransferBuffer(dev, upload_buf_, true);
        if (!ptr) return 0;
        for (int i = 0; i < n; ++i) {
            ptr[i*2 + 0] = npc_x[i];
            ptr[i*2 + 1] = npc_z[i];
        }
        SDL_UnmapGPUTransferBuffer(dev, upload_buf_);

        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src{ upload_buf_, 0 };
        SDL_GPUBufferRegion dst{ pos_buf_, 0, (uint32_t)(n * 2 * sizeof(float)) };
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
        SDL_EndGPUCopyPass(cp);
    }

    // ── Dispatch compute ──────────────────────────────────────────────────────
    {
        GpuComputeStorageBindings bindings;
        bindings.cmd = cmd;
        // RW buffer: visibility output (set=1, binding=0)
        bindings.rw_buffers[0] = { vis_buf_, false };
        bindings.num_rw_buffers = 1;
        // RO buffer: positions (set=0, binding=0)
        bindings.ro_buffers[0]  = pos_buf_;
        bindings.num_ro_buffers = 1;

        GpuComputePass pass;
        pass.Begin(&pipeline_, bindings);

        // CullUBO: mvp (64B) + npc_count (4B) + npc_radius (4B) + pad (8B) = 80B
        struct CullUBO {
            float mvp[16];
            int   npc_count;
            float npc_radius;
            float _pad[2];
        } ubo;
        memcpy(ubo.mvp, mvp16, 64);
        ubo.npc_count  = n;
        ubo.npc_radius = npc_radius;
        ubo._pad[0] = ubo._pad[1] = 0.f;
        pass.PushUniforms(0, &ubo, sizeof(ubo));

        uint32_t groups = ((uint32_t)n + 63u) / 64u;
        pass.Dispatch(groups, 1, 1);
        pass.End();
    }

    // ── Download visibility result ────────────────────────────────────────────
    {
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUBufferRegion src{ vis_buf_, 0, (uint32_t)(n * sizeof(uint32_t)) };
        SDL_GPUTransferBufferLocation dst{ dl_buf_, 0 };
        SDL_DownloadFromGPUBuffer(cp, &src, &dst);
        SDL_EndGPUCopyPass(cp);
    }

    // Submit + sync (blocking readback, Intel HD 520 shared memory — fast).
    GpuDevice::Get().Submit(cmd);
    SDL_WaitForGPUIdle(dev);

    // ── Read back result ──────────────────────────────────────────────────────
    const uint32_t* ptr = (const uint32_t*)SDL_MapGPUTransferBuffer(dev, dl_buf_, false);
    if (ptr) {
        int visible = 0;
        for (int i = 0; i < n; ++i) {
            results_[i] = ptr[i];
            if (ptr[i]) ++visible;
        }
        SDL_UnmapGPUTransferBuffer(dev, dl_buf_);
        last_count_   = n;
        last_visible_ = visible;
    }

    return last_visible_;
}

} // namespace md
#endif // MD_SDL_GPU
