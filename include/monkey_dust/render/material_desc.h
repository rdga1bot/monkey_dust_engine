#pragma once
#include <monkey_dust/render/gpu_hal.h>
#include <cstdint>

// ── MaterialDesc ──────────────────────────────────────────────────────────────
// OGRE v1-inspired declarative material descriptor.
// Parsed from a simple JSON format; used to auto-construct a GpuPipeline
// without hand-writing pipeline state per material.
//
// JSON format example:
//   {
//     "name":          "mat_npc_pbr",
//     "vert_shader":   "shaders/npc_instanced.vert.spv",
//     "frag_shader":   "shaders/npc_instanced.frag.spv",
//     "blend":         "opaque",      // "opaque" | "alpha" | "additive"
//     "depth_write":   true,
//     "depth_test":    true,
//     "cull":          "back",        // "back" | "front" | "none"
//     "topology":      "triangles"    // "triangles" | "lines" | "points"
//   }
//
// Usage:
//   MaterialDesc desc;
//   MaterialDesc::LoadFromJson(desc, json_str);
//   GpuPipeline* pipe = MaterialDesc::BuildPipeline(desc, layout);

static constexpr int MD_MAT_NAME_LEN   = 64;
static constexpr int MD_MAT_SHADER_LEN = 128;

enum class MatBlend : uint8_t { Opaque = 0, Alpha = 1, Additive = 2 };
enum class MatCull  : uint8_t { Back   = 0, Front = 1, None     = 2 };

struct MaterialDesc {
    char     name       [MD_MAT_NAME_LEN]    = {};
    char     vert_shader[MD_MAT_SHADER_LEN]  = {};
    char     frag_shader[MD_MAT_SHADER_LEN]  = {};
    MatBlend blend       = MatBlend::Opaque;
    MatCull  cull        = MatCull::Back;
    GpuTopology topology = GpuTopology::TRIANGLES;
    bool     depth_write = true;
    bool     depth_test  = true;
    uint8_t  _pad[2]     = {};

    // Parse from a JSON string (strstr-based, no external library).
    // Returns false if required fields are missing.
    static bool LoadFromJson(MaterialDesc& out, const char* json) noexcept;

    // Build a GpuPipeline::Desc from this descriptor.
    // Caller provides vertex layout; shaders are referenced by vert_shader / frag_shader.
    static GpuPipeline::Desc BuildPipelineDesc(const MaterialDesc& md,
                                               const GpuVertexLayout& layout) noexcept;
};
