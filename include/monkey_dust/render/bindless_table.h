#pragma once
// bindless_table.h — Virtual bindless texture array for SDL_GPU.
//
// Vulkan adaptation: VK_EXT_descriptor_indexing allows texture[mat_id] in shaders.
// Intel HD 520 supports this with max 128 descriptors.
// SDL_GPU doesn't expose raw descriptor indexing, so we implement a "virtual" version:
//   - Fixed array of up to MAX_SLOTS textures
//   - Shader uses a push constant (tex_id) to select from a hardcoded sampler array
//   - OR: swap sampler binding before each draw (1 call instead of full descriptor set)
//
// Usage:
//   static BindlessTable<32> s_npc_textures{"npc_bindless"};
//   int slot = s_npc_textures.Register(tex, smp);
//   // In shader push constant: tex_id = slot
//   // Before draw: s_npc_textures.Bind(pass, slot);

#include <cstdint>
#include <cstring>
#include <cstdio>

#ifdef MD_SDL_GPU
#include <SDL3/SDL_gpu.h>
#endif

template<int MAX_SLOTS = 32>
struct BindlessTable {
    static_assert(MAX_SLOTS <= 128, "Intel HD 520 supports max 128 descriptors");

    const char* name = "bindless";

    struct Slot {
        SDL_GPUTexture* texture = nullptr;
        SDL_GPUSampler* sampler = nullptr;
        bool            used    = false;
    };

    // Register a texture+sampler pair. Returns slot index [0..MAX_SLOTS).
    // Returns -1 if table is full. Idempotent: same texture returns same slot.
    int Register(SDL_GPUTexture* tex, SDL_GPUSampler* smp) {
        // Check if already registered (pointer identity = same GPU object).
        for (int i = 0; i < MAX_SLOTS; ++i)
            if (slots_[i].used && slots_[i].texture == tex) return i;
        // Find free slot.
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (!slots_[i].used) {
                slots_[i] = { tex, smp, true };
                return i;
            }
        }
        fprintf(stderr, "[BindlessTable '%s'] full (max=%d)\n", name, MAX_SLOTS);
        return -1;
    }

    void Unregister(int slot) {
        if (slot >= 0 && slot < MAX_SLOTS) slots_[slot] = {};
    }

    void Clear() {
        for (int i = 0; i < MAX_SLOTS; ++i) slots_[i] = {};
    }

    // Bind texture at slot to fragment sampler binding N.
    // Call once before each draw instead of switching descriptor sets.
    // With pipeline cache (pattern 2), per-draw binding is the only per-draw cost.
    void BindFrag(SDL_GPURenderPass* pass, int slot, int binding = 0) const {
        if (slot < 0 || slot >= MAX_SLOTS || !slots_[slot].used || !pass) return;
        SDL_GPUTextureSamplerBinding b;
        b.texture = slots_[slot].texture;
        b.sampler = slots_[slot].sampler;
        SDL_BindGPUFragmentSamplers(pass, (Uint32)binding, &b, 1);
    }

    // Bind all registered textures as a sampler array starting at binding_base.
    // Use for passes that need access to the entire table.
    // Returns actual count of valid slots bound.
    int BindAll(SDL_GPURenderPass* pass, int binding_base = 0) const {
        int n = 0;
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (!slots_[i].used) continue;
            SDL_GPUTextureSamplerBinding b;
            b.texture = slots_[i].texture;
            b.sampler = slots_[i].sampler;
            SDL_BindGPUFragmentSamplers(pass, (Uint32)(binding_base + i), &b, 1);
            ++n;
        }
        return n;
    }

    int  Count()    const { int n=0; for(int i=0;i<MAX_SLOTS;++i) n+=slots_[i].used; return n; }
    int  Capacity() const { return MAX_SLOTS; }
    bool Full()     const { return Count() == MAX_SLOTS; }

    const Slot& operator[](int i) const { return slots_[i]; }

private:
    Slot slots_[MAX_SLOTS] = {};
};
