#include <monkey_dust/render/gpu_texture_pool.h>
#include <monkey_dust/render/gpu_hal.h>
#include <monkey_dust/platform/md_log.h>

namespace md {

SDL_GPUTexture* GpuTexturePool::Acquire(md::GpuDeviceHandle dev, const RGTextureDesc& desc) {
    dev_ = dev;  // cached for Shutdown(); all callers share the one GpuDevice
    // Reuse a released entry with an identical desc first.
    for (int i = 0; i < count_; ++i) {
        Entry& e = entries_[i];
        if (!e.in_use && e.desc == desc) {
            e.in_use = true;
            return e.tex;
        }
    }
    // No match — create a new one if there's room.
    if (count_ >= MAX_POOLED) {
        MD_LOG(MD_LOG_WARNING,
               "[GpuTexturePool] MAX_POOLED=%d reached, cannot acquire '%s' (%dx%d)",
               MAX_POOLED, desc.debug_name, desc.width, desc.height);
        return nullptr;
    }

    SDL_GPUTextureCreateInfo ti = {};
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.width                = (Uint32)desc.width;
    ti.height               = (Uint32)desc.height;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = 1;
    ti.format               = desc.format;
    ti.usage                = desc.usage;

    SDL_GPUTexture* tex = GpuCreateTexture(dev, &ti);
    if (!tex) {
        MD_LOG(MD_LOG_WARNING, "[GpuTexturePool] SDL_CreateGPUTexture failed for '%s' (%dx%d)",
               desc.debug_name, desc.width, desc.height);
        return nullptr;
    }

    Entry& e = entries_[count_++];
    e.tex    = tex;
    e.desc   = desc;
    e.in_use = true;
    return tex;
}

void GpuTexturePool::Release(SDL_GPUTexture* tex) {
    if (!tex) return;
    for (int i = 0; i < count_; ++i) {
        if (entries_[i].tex == tex) {
            entries_[i].in_use = false;
            return;
        }
    }
}

void GpuTexturePool::EndFrame() {
    for (int i = 0; i < count_; ++i) {
        if (entries_[i].in_use) {
            MD_LOG(MD_LOG_WARNING,
                   "[GpuTexturePool] '%s' still in-use at EndFrame — caller forgot Release()",
                   entries_[i].desc.debug_name);
            entries_[i].in_use = false;
        }
    }
}

void GpuTexturePool::Shutdown() {
    if (dev_) {
        for (int i = 0; i < count_; ++i) {
            if (entries_[i].tex) GpuReleaseTexture(dev_, entries_[i].tex);
        }
    }
    count_ = 0;
    dev_   = nullptr;
}

} // namespace md
